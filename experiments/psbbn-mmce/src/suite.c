/* PSBBN diagnostic suite v0.4. MMCE images are always opened read-only.
 * The only storage writes are diagnostic reports on the USB drive.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <kernel.h>
#include <timer.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include "suite_rpc.h"
#include "fhi_fileid_config.h"

#define MOD(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
MOD(iomanX); MOD(fileXio); MOD(sio2man); MOD(mmceman);
MOD(mmcefhi); MOD(atad_emu); MOD(suite_rpc);
MOD(bdm); MOD(bdmfs_fatfs); MOD(usbd); MOD(usbmass_bd);
#define LOAD(n) module(#n, n##_irx, size_##n##_irx)
static char log_text[32768], saved_path[80];
static unsigned int log_used;
static int fxio, usb_loaded, rpc_dead;
static unsigned char buffer[32768 + 128] __attribute__((aligned(64)));
static struct suite_reply reply __attribute__((aligned(64)));
static struct suite_request request __attribute__((aligned(64)));
static SifRpcClientData_t rpc __attribute__((aligned(64)));
static unsigned int speeds[3];
static const char *names[] = {"512-byte fixture", "File random/mixed reads", "File full 8 MiB scan",
    "File repeat/seek stability", "Handle + IOP reboot", "FHI capacity", "FHI random/full scan",
    "Virtual ATA capacity", "ATA random/full scan", "Request bounds checks", "MMCE reinit after ATA"};
static int outcomes[11]; /* 0 skipped, 1 pass, -1 fail */

static void report(const char *format, ...)
{
    char line[512];
    va_list args;
    unsigned int size;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    scr_printf("%s", line);
    size = strlen(line);
    if (size < sizeof(log_text) - log_used) {
        memcpy(log_text + log_used, line, size + 1);
        log_used += size;
    }
}

static void result(int index, int pass)
{
    outcomes[index] = pass ? 1 : -1;
    report("[%s] %s\n", pass ? "PASS" : "FAIL", names[index]);
}

static int module(const char *name, unsigned char *data, unsigned int size)
{
    int rv = -1, id = SifExecModuleBuffer(data, size, 0, NULL, &rv);
    report("Module %s: id=%d start=%d\n", name, id, rv);
    return id >= 0 && (rv == 0 || rv == 2);
}

static void reset_iop(void)
{
    if (fxio) { fileXioExit(); fxio = 0; }
    SifExitIopHeap(); SifLoadFileExit(); SifExitRpc();
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0); SifInitIopHeap(); SifLoadFileInit();
    sbv_patch_enable_lmb();
    usb_loaded = 0;
    memset(&rpc, 0, sizeof(rpc));
}

static int bind_service(unsigned int id)
{
    int i;
    memset(&rpc, 0, sizeof(rpc));
    for (i = 0; i < 100; ++i) {
        if (SifBindRpc(&rpc, id, 0) < 0) return 0;
        if (rpc.server) return 1;
        usleep(50000);
    }
    report("RPC service %08x missing after 5s\n", id);
    return 0;
}

static int file_environment(void)
{
    if (!LOAD(iomanX) || !LOAD(fileXio) || !bind_service(FILEXIO_IRX)) return 0;
    if (fileXioInit() < 0) return 0;
    fxio = 1;
    return 1;
}

static int start_usb(void)
{
    if (usb_loaded) return 1;
    if (!LOAD(bdm) || !LOAD(bdmfs_fatfs) || !LOAD(usbd) || !LOAD(usbmass_bd)) return 0;
    usleep(2000000);
    usb_loaded = 1;
    return 1;
}

static int save_report(void)
{
    static const char *paths[] = {"mass0:/PSBBN-DIAG-v04.log", "mass1:/PSBBN-DIAG-v04.log", "mass:/PSBBN-DIAG-v04.log"};
    int i, fd, written, rv;
    if (!fxio || !start_usb()) return 0;
    for (i = 0; i < 3; ++i) {
        fd = fileXioOpen(paths[i], FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
        if (fd < 0) continue;
        written = 0;
        while (written < (int)log_used) {
            rv = fileXioWrite(fd, log_text + written, log_used - written);
            if (rv <= 0) break;
            written += rv;
        }
        rv = fileXioClose(fd);
        if (written == (int)log_used && rv >= 0) {
            snprintf(saved_path, sizeof(saved_path), "%s", paths[i]);
            return 1;
        }
    }
    return 0;
}

static unsigned int crc32(const unsigned char *p, unsigned int size)
{
    unsigned int crc = ~0U, i, bit;
    for (i = 0; i < size; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int verify(const unsigned char *p, u32 lba, unsigned int size)
{
    unsigned int i = suite_first_mismatch(p, lba, size);
    if (i != size) {
        report("Mismatch: LBA=%u byte=%u got=%02x want=%02x\n", lba + i / 512, i % 512, p[i], suite_byte(lba + i / 512, i % 512));
        return 0;
    }
    return 1;
}

static int guards(unsigned int size)
{
    unsigned int i;
    for (i = 0; i < 64; ++i)
        if (buffer[i] != 0x5a || buffer[64 + size + i] != 0x5a) return 0;
    return 1;
}

static int file_read(int fd, u32 lba, unsigned int count)
{
    int rv, used = 0;
    unsigned int size = count * 512;
    s64 offset = (s64)lba * 512;
    memset(buffer, 0x5a, sizeof(buffer));
    if (fileXioLseek64(fd, offset, FIO_SEEK_SET) != offset) {
        report("File seek failed: LBA=%u\n", lba);
        return 0;
    }
    while (used < (int)size) {
        rv = fileXioRead(fd, buffer + 64 + used, size - used);
        if (rv <= 0) { report("File read failed: LBA=%u read=%d result=%d\n", lba, used, rv); return 0; }
        used += rv;
    }
    if (!guards(size)) { report("FILEXIO buffer guard changed\n"); return 0; }
    return verify(buffer + 64, lba, size);
}

static int remote(int function, u32 lba, u32 count)
{
    int rv, i;
    request.lba = lba; request.count = count;
    if (rpc_dead) return 0;
    rv = SifCallRpc(&rpc, function, SIF_RPC_M_NOWAIT, &request, sizeof(request), &reply, sizeof(reply), NULL, NULL);
    if (rv < 0) { report("RPC call failed: %d\n", rv); rpc_dead = 1; return 0; }
    for (i = 0; i < 10000 && SifCheckStatRpc(&rpc); ++i) usleep(1000);
    if (SifCheckStatRpc(&rpc)) {
        report("RPC timeout: function=%d LBA=%u count=%u\n", function, lba, count);
        rpc_dead = 1;
        return 0;
    }
    return 1;
}

static int remote_read(int function, u32 lba, unsigned int count)
{
    if (!remote(function, lba, count)) return 0;
    if (reply.result != (function == 1 ? 1 : 0)) {
        report("Read failed: function=%d LBA=%u count=%u result=%d\n", function, lba, count, reply.result);
        return 0;
    }
    return verify(reply.data, lba, count * 512);
}

static void speed(int index, unsigned int bytes, u64 elapsed)
{
    u64 micros = elapsed * 1000000 / kBUSCLK;
    if (!micros) { report("Timer unavailable; throughput not measured\n"); return; }
    speeds[index] = (unsigned int)((u64)bytes * 1000000 / (micros * 1024));
    report("8 MiB verified: %u KiB/s, %llu ms (includes verification/RPC)\n",
           speeds[index], (unsigned long long)(micros / 1000));
}

static int scan(int fd, int function, int speed_index)
{
    u32 lba;
    u64 start = GetTimerSystemTime();
    for (lba = 0; lba < SUITE_SECTORS; lba += SUITE_MAX_SECTORS) {
        if (lba % 2048 == 0) report("Scan %s: %u/8 MiB\n", function == 1 ? "FHI" : function == 2 ? "ATA" : "file", lba / 2048);
        if (function ? !remote_read(function, lba, SUITE_MAX_SECTORS) : !file_read(fd, lba, SUITE_MAX_SECTORS)) return 0;
    }
    speed(speed_index, SUITE_SECTORS * 512, GetTimerSystemTime() - start);
    return 1;
}

static int mixed(int fd, int function)
{
    static const unsigned int counts[] = {1, 2, 3, 8, 32, 64};
    u32 seed = 0x12345678, lba;
    unsigned int i, count;
    for (i = 0; i < 36; ++i) {
        seed = seed * 1664525U + 1013904223U;
        count = counts[i % 6];
        lba = seed % (SUITE_SECTORS - count + 1);
        if (i == 0) lba = 0;
        if (i == 1) lba = SUITE_SECTORS - count;
        report("Mixed %s %u/36: LBA=%u count=%u\n", function == 1 ? "FHI" : function == 2 ? "ATA" : "file", i + 1, lba, count);
        if (function ? !remote_read(function, lba, count) : !file_read(fd, lba, count)) return 0;
    }
    report("36 mixed-size random reads verified, including disk boundaries\n");
    return 1;
}

static int choose_slot(void)
{
    char path[64];
    int slot, fd, ping, count, i, selected = -1;
    for (slot = 0; slot < 2; ++slot) {
        snprintf(path, sizeof(path), "mmce%d:", slot);
        ping = fileXioDevctl(path, 1, NULL, 0, NULL, 0);
        if (ping < 0) report("%s no valid PING (slot may be empty)\n", path);
        else report("%s protocol=%u product=%u revision=%u\n", path,
                    ((unsigned int)ping >> 16) & 255, ((unsigned int)ping >> 8) & 255, (unsigned int)ping & 255);
        snprintf(path, sizeof(path), "mmce%d:/PSBBN/probe.dat", slot);
        fd = fileXioOpen(path, FIO_O_RDONLY, 0);
        if (fd < 0) { report("Fixture open: %d\n", fd); continue; }
        count = fileXioRead(fd, buffer, 512);
        fileXioClose(fd);
        for (i = 0; i < count && i < 512; ++i) if (buffer[i] != (unsigned char)i) break;
        report("Fixture slot %d: bytes=%d CRC=%08x\n", slot, count, count == 512 ? crc32(buffer, 512) : 0);
        if (count == 512 && i == 512 && selected < 0) selected = slot;
    }
    return selected;
}

static int prepare_image(int slot)
{
    char path[64];
    int fd, id, i, pass;
    s64 size;
    struct fhi_fileid *settings = NULL;
    _Static_assert(sizeof(struct fhi_fileid) == 80, "MMCEFHI configuration ABI");
    for (i = 0; i + (int)sizeof(*settings) <= (int)size_mmcefhi_irx; i += 4) {
        if (*(u32 *)(mmcefhi_irx + i) == MODULE_SETTINGS_MAGIC) {
            if (settings) { report("Ambiguous FHI settings; stopping\n"); return 0; }
            settings = (struct fhi_fileid *)(mmcefhi_irx + i);
        }
    }
    if (!settings) { report("FHI settings not found\n"); return 0; }
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/diagnostic.img", slot);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    report("Test disk open: %s -> %d\n", path, fd);
    if (fd < 0) { report("Copy supplied PSBBN/diagnostic.img and rerun\n"); return 0; }
    size = fileXioLseek64(fd, 0, FIO_SEEK_END);
    if (size != (s64)SUITE_SECTORS * 512) {
        report("Test disk size wrong: %lld; expected 8388608\n", (long long)size);
        fileXioClose(fd); return 0;
    }
    result(1, mixed(fd, 0));
    result(2, scan(fd, 0, 0));
    pass = 1;
    report("Filesystem repeat/seek stability: 32 first/last/first cycles\n");
    for (i = 0; i < 32 && pass; ++i)
        pass = file_read(fd, 0, 1) && file_read(fd, SUITE_SECTORS - 1, 1) && file_read(fd, 0, 1);
    result(3, pass);
    /* Verified MMCEMAN ioctl2 0x80 returns the device-side file ID. */
    id = fileXioIoctl2(fd, 0x80, NULL, 0, NULL, 0);
    report("Persistent MMCE file ID: %d\n", id);
    if (id < 0 || id >= 250) { fileXioClose(fd); return 0; }
    memset(settings, 0, sizeof(*settings));
    settings->magic = MODULE_SETTINGS_MAGIC;
    settings->devNr = slot;
    settings->file[FHI_FID_ATA0].id = id;
    settings->file[FHI_FID_ATA0].size = size;
    /* Keep the remote file open across IOP reset, as Neutrino does. */
    return 1;
}

static void virtual_tests(void)
{
    int pass;
    report("\nSTAGE 2: IOP reboot, MMCEFHI + Neutrino ATAD\n");
    reset_iop();
    if (!LOAD(sio2man) || !LOAD(mmcefhi) || !LOAD(atad_emu) || !LOAD(suite_rpc) || !bind_service(SUITE_RPC_ID)) {
        result(4, 0); return;
    }
    if (!remote(3, 0, 0)) { result(4, 0); return; }
    report("Capacity: FHI=%u ATAD=%u exists=%d packet=%d security=%08x\n",
           reply.fhi_sectors, reply.sectors, reply.exists, reply.packet, reply.security);
    result(5, reply.fhi_sectors == SUITE_SECTORS);
    result(7, reply.sectors == SUITE_SECTORS && reply.exists && !reply.packet);
    pass = remote_read(1, 0, 1);
    result(4, pass);
    if (!rpc_dead) result(6, mixed(-1, 1) && scan(-1, 1, 1));
    if (!rpc_dead) result(8, mixed(-1, 2) && scan(-1, 2, 2));
    if (!rpc_dead) {
        pass = remote(1, 0, 0) && reply.result == -22;
        pass = pass && remote(2, SUITE_SECTORS, 1) && reply.result == -22;
        pass = pass && remote(2, SUITE_SECTORS - 1, 2) && reply.result == -22;
        result(9, pass);
    }
}

int main(int argc, char **argv)
{
    int slot = -1, image_ready = 0, i, fd, n, saved = 0;
    char romver[17] = {0};
    (void)argc; (void)argv;
    init_scr(); _ps2sdk_init_timer(); SifInitRpc(0);
    report("PSBBN AUTOMATIC DIAGNOSTIC SUITE v0.4\n");
    report("MMCE image read-only. USB report is the only write.\n");
    report("STAGE 1: MMCE filesystem, fixture, 8 MiB disk\n");
    reset_iop();
    if (file_environment() && LOAD(sio2man) && LOAD(mmceman)) {
        fd = fileXioOpen("rom0:ROMVER", FIO_O_RDONLY, 0);
        if (fd >= 0) {
            n = fileXioRead(fd, romver, 16); fileXioClose(fd);
            if (n > 0) report("ROMVER: %.16s\n", romver);
        }
        slot = choose_slot(); result(0, slot >= 0);
        if (slot >= 0) {
            report("Checkpoint before filesystem stress tests.\n");
            saved = save_report();
            report("Early USB checkpoint: %s\n", saved ? saved_path : "unavailable");
            image_ready = prepare_image(slot);
        }
        report("Checkpoint before virtual driver test.\n");
        saved = save_report();
        report("USB checkpoint: %s\n", saved ? saved_path : "unavailable; photograph summary");
        if (image_ready) virtual_tests();
    } else report("Filesystem startup failed\n");
    if (image_ready) {
        report("\nSTAGE 3: restore MMCE after virtual ATA\n");
        reset_iop();
        if (file_environment() && LOAD(sio2man) && LOAD(mmceman)) result(10, choose_slot() == slot);
        else result(10, 0);
    }
    report("\nRESULTS\n");
    for (i = 0; i < 11; ++i)
        report("%s: %s\n", names[i], outcomes[i] > 0 ? "PASS" : outcomes[i] < 0 ? "FAIL" : "SKIPPED");
    report("PSBBN/APA/PFS and offsets above 4 GiB remain untested.\n");
    saved = save_report();
    scr_clear(); scr_setXY(0, 0);
    scr_printf("PSBBN AUTOMATIC SUITE v0.4 - FINISHED\n\n");
    for (i = 0; i < 11; ++i)
        scr_printf("%-26s %s\n", names[i], outcomes[i] > 0 ? "PASS" : outcomes[i] < 0 ? "FAIL" : "SKIPPED");
    scr_printf("\nVerified KiB/s: file=%u FHI=%u ATA=%u\n", speeds[0], speeds[1], speeds[2]);
    scr_printf("\nUSB report: %s\n", saved ? saved_path : "unavailable (take a photo)");
    if (slot >= 0 && !image_ready) scr_printf("Test image missing/invalid; see log or copy PSBBN folder.\n");
    scr_printf("\nNo PSBBN boot is claimed. Power off to exit.\n");
    SleepThread();
    return 0;
}
