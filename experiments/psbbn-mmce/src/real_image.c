/* PSBBN real-image preflight. Its checks are read-only.
 * PSBBN_BOOT_BUILD adds a gated read/write Linux boot attempt afterwards.
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
#include "real_checks.h"

#define MOD(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
MOD(iomanX); MOD(fileXio); MOD(sio2man); MOD(mmceman);
MOD(mmcefhi); MOD(atad_emu); MOD(suite_rpc);
MOD(bdm); MOD(bdmfs_fatfs); MOD(usbd); MOD(usbmass_bd);
#define LOAD(n) module(#n, n##_irx, size_##n##_irx)
static char log_text[32768], saved_path[80];
static unsigned int log_used;
static int fxio, usb_loaded, rpc_dead;
#ifdef PSBBN_BOOT_BUILD
static int boot_read_handle_closed;
static struct real_session_samples boot_samples;
#endif
static unsigned char buffer[32768 + 128] __attribute__((aligned(64)));
static struct suite_reply reply __attribute__((aligned(64)));
static struct suite_request request __attribute__((aligned(64)));
static SifRpcClientData_t rpc __attribute__((aligned(64)));
MOD(atad_ro); MOD(apa_ro); MOD(pfs_ro);
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


static int module(const char *name, unsigned char *data, unsigned int size)
{
    int rv = -1, id;
    report("Starting module %s...\n", name);
    id = SifExecModuleBuffer(data, size, 0, NULL, &rv);
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
#ifdef PSBBN_BOOT_BUILD
    static const char *paths[] = {"mass0:/PSBBN-BOOT-v0612.log", "mass1:/PSBBN-BOOT-v0612.log", "mass:/PSBBN-BOOT-v0612.log"};
#else
    static const char *paths[] = {"mass0:/PSBBN-REAL-v05.log", "mass1:/PSBBN-REAL-v05.log", "mass:/PSBBN-REAL-v05.log"};
#endif
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


static int guards(unsigned int size)
{
    unsigned int i;
    for (i = 0; i < 64; ++i)
        if (buffer[i] != 0x5a || buffer[64 + size + i] != 0x5a) return 0;
    return 1;
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

static struct real_checks checks __attribute__((aligned(64)));
static const char *test_names[] = {"Host fingerprints", "7 GiB image size", "File reads >4 GiB",
    "FHI/ATA capacity", "FHI sampled reads", "ATA sampled reads", "APA partition discovery",
    "Read-only PFS mount", "Complete osdboot.elf", "Complete vmlinux", "MMCE restored"};
static int outcomes[11];

static void result(int index, int pass)
{
    outcomes[index] = pass ? 1 : -1;
    report("[%s] %s\n", pass ? "PASS" : "FAIL", test_names[index]);
}

static int read_exact(int fd, void *data, unsigned int size)
{
    unsigned int used = 0;
    int rv;
    while (used < size) {
        rv = fileXioRead(fd, (unsigned char *)data + used, size - used);
        if (rv <= 0 || (unsigned int)rv > size - used) return 0;
        used += rv;
    }
    return 1;
}

static int read_checks(int slot)
{
    char path[80];
    int fd, pass = 0;
    s64 size;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/real-checks.bin", slot);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) return 0;
    size = fileXioLseek64(fd, 0, FIO_SEEK_END);
    if (size >= 32 && size <= (s64)sizeof(checks) && fileXioLseek64(fd, 0, FIO_SEEK_SET) == 0 &&
        read_exact(fd, &checks, size)) pass = real_checks_valid(&checks, size);
    fileXioClose(fd);
    report("Fingerprint profile %s: %s\n", path, pass ? "valid" : "invalid");
    return pass;
}

static int sample_reads(int fd, int function)
{
    unsigned int i, got;
    for (i = 0; i < checks.sample_count; ++i) {
        struct real_sample *s = &checks.samples[i];
        unsigned int bytes = s->count * 512;
        s64 offset = (s64)s->lba * 512;
        if (function) {
            if (!remote(function, s->lba, s->count) || reply.result != (function == 1 ? 1 : 0)) {
                report("Remote failure: function=%d LBA=%u result=%d\n", function, s->lba, reply.result);
                return 0;
            }
            got = crc32(reply.data, bytes);
        } else {
            memset(buffer, 0x5a, sizeof(buffer));
            if (fileXioLseek64(fd, offset, FIO_SEEK_SET) != offset ||
                !read_exact(fd, buffer + 64, bytes) || !guards(bytes)) {
                report("File seek/read/guard failed at LBA=%u\n", s->lba);
                return 0;
            }
            got = crc32(buffer + 64, bytes);
        }
        report("Sample %u/%u route=%d LBA=%u count=%u CRC=%08x\n",
               i + 1, checks.sample_count, function, s->lba, s->count, got);
#ifdef PSBBN_BOOT_BUILD
        if (!function) {
            unsigned int repeated;
            memset(buffer, 0x5a, sizeof(buffer));
            if (fileXioLseek64(fd, offset, FIO_SEEK_SET) != offset ||
                !read_exact(fd, buffer + 64, bytes) || !guards(bytes)) {
                report("Repeated file read/guard failed at LBA=%u\n", s->lba);
                return 0;
            }
            repeated = crc32(buffer + 64, bytes);
            if (!real_session_learn(&boot_samples, i, got, repeated)) {
                report("Repeated read mismatch: first=%08x second=%08x\n", got, repeated);
                return 0;
            }
            if (got != s->crc)
                report("Live sample differs from original reference (%08x); repeated read matches.\n", s->crc);
        } else if (!real_session_matches(&boot_samples, i, got)) {
            report("Live FILEXIO reference mismatch; stopping this route\n");
            return 0;
        }
#else
        if (got != s->crc) {
            report("Expected CRC=%08x; stopping this route\n", s->crc);
            return 0;
        }
#endif
    }
    return 1;
}

static int prepare_image(int slot)
{
    char path[80];
    int fd, id, pass;
    unsigned int i;
    s64 size;
    struct fhi_fileid *settings = NULL;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/psbbn.img", slot);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    report("Image %s: fd=%d\n", path, fd);
    if (fd < 0) return 0;
    size = fileXioLseek64(fd, 0, FIO_SEEK_END);
    report("Image size=%lld expected=%llu\n", (long long)size, (unsigned long long)checks.image_size);
    result(1, size == (s64)checks.image_size);
    if (outcomes[1] < 0) { fileXioClose(fd); return 0; }
#ifdef PSBBN_BOOT_BUILD
    memset(&boot_samples, 0, sizeof(boot_samples));
    report("Boot checks use repeated live samples; original boot-file fingerprints stay enforced.\n");
#endif
    pass = sample_reads(fd, 0);
    result(2, pass);
    if (!pass) { fileXioClose(fd); return 0; }
    for (i = 0; i + sizeof(*settings) <= size_mmcefhi_irx; i += 4) {
        if (*(u32 *)(mmcefhi_irx + i) == MODULE_SETTINGS_MAGIC) {
            if (settings) { fileXioClose(fd); return 0; }
            settings = (struct fhi_fileid *)(mmcefhi_irx + i);
        }
    }
    id = fileXioIoctl2(fd, 0x80, NULL, 0, NULL, 0);
    report("Persistent image handle=%d\n", id);
    if (!settings || id < 0 || id >= 250) { fileXioClose(fd); return 0; }
    memset(settings, 0, sizeof(*settings));
    settings->magic = MODULE_SETTINGS_MAGIC;
    settings->devNr = slot;
    settings->file[FHI_FID_ATA0].id = id;
    settings->file[FHI_FID_ATA0].size = size;
    return 1; /* The remote handle deliberately stays open across reset. */
}

static int partition_discovery(void)
{
    iox_dirent_t entry;
    int fd, rv, count = 0, system = 0, linux1 = 0, linux9 = 0;
    fd = fileXioDopen("hdd0:");
    report("APA directory fd=%d\n", fd);
    if (fd < 0) return 0;
    while ((rv = fileXioDread(fd, &entry)) > 0 && count < 64) {
        entry.name[sizeof(entry.name) - 1] = 0;
        report("APA: %s mode=%08x attr=%08x size=%u\n", entry.name,
               entry.stat.mode, entry.stat.attr, entry.stat.size);
        system |= !strcmp(entry.name, "__system");
        linux1 |= !strcmp(entry.name, "__linux.1");
        linux9 |= !strcmp(entry.name, "__linux.9");
        ++count;
    }
    fileXioDclose(fd);
    return rv == 0 && count > 0 && count < 64 && system && linux1 && linux9;
}

static int check_boot_file(int index, const char *path)
{
    int fd, pass = 1;
    unsigned int used = 0, crc = ~0U, chunk;
    s64 size;
    struct real_file *expected = &checks.files[index];
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) { report("Boot file open failed: %s -> %d\n", path, fd); return 0; }
    size = fileXioLseek64(fd, 0, FIO_SEEK_END);
    if (size != expected->size || fileXioLseek64(fd, 0, FIO_SEEK_SET) != 0) pass = 0;
    while (pass && used < expected->size) {
        chunk = expected->size - used;
        if (chunk > 32768) chunk = 32768;
        if (!read_exact(fd, buffer, chunk)) { pass = 0; break; }
        if (!used && (chunk < 20 || memcmp(buffer, "\177ELF", 4) || buffer[4] != 1 || buffer[5] != 1 ||
                      buffer[18] != 8 || buffer[19] != 0)) pass = 0;
        crc = real_crc_update(crc, buffer, chunk);
        used += chunk;
    }
    fileXioClose(fd);
    report("%s bytes=%u/%u CRC=%08x expected=%08x\n", path, used, expected->size, ~crc, expected->crc);
    return pass && ~crc == expected->crc;
}

static void virtual_tests(void)
{
    int pass, rv;
    report("Checkpoint: resetting into read-only virtual ATA\n");
    save_report();
    reset_iop();
    if (!LOAD(sio2man) || !LOAD(mmcefhi) || !LOAD(atad_ro) || !LOAD(suite_rpc) || !bind_service(SUITE_RPC_ID)) {
        result(3, 0); return;
    }
    pass = remote(3, 0, 0) && reply.fhi_sectors == REAL_SECTORS && reply.sectors == REAL_SECTORS &&
           reply.exists && !reply.packet;
    result(3, pass);
    if (!pass) return;
    result(4, sample_reads(-1, 1));
    if (rpc_dead) return;
    result(5, sample_reads(-1, 2));
    if (rpc_dead || outcomes[4] < 0 || outcomes[5] < 0) return;
    if (!file_environment()) return;
    report("Checkpoint: before APA startup (no DEV9 module)\n"); save_report();
    if (!LOAD(apa_ro)) { result(6, 0); return; }
    result(6, partition_discovery());
    if (outcomes[6] < 0) return;
    report("Checkpoint: before PFS startup/mount\n"); save_report();
    if (!LOAD(pfs_ro)) { result(7, 0); return; }
    rv = fileXioMount("pfs0:", "hdd0:__system", FIO_MT_RDONLY);
    report("Mount __system read-only=%d\n", rv);
    result(7, rv == 0);
    if (rv) return;
    report("Checkpoint: before complete boot-file reads\n"); save_report();
    result(8, check_boot_file(0, "pfs0:/p2lboot/osdboot.elf"));
    save_report();
    result(9, check_boot_file(1, "pfs0:/p2lboot/vmlinux"));
    rv = fileXioUmount("pfs0:"); report("Unmount __system=%d\n", rv);
#ifdef PSBBN_BOOT_BUILD
    if (!rv && outcomes[8] == 1 && outcomes[9] == 1) {
        boot_read_handle_closed = bind_service(SUITE_RPC_ID) && remote(4, 0, 0) && reply.result == 0;
        report("Close read-only preflight image handle: %s\n", boot_read_handle_closed ? "PASS" : "FAIL");
    }
#endif
    save_report();
}

#ifdef PSBBN_BOOT_BUILD
#include "boot_launch.h"
#endif

int main(int argc, char **argv)
{
    int slot, selected = -1, ready = 0, i, saved;
    (void)argc; (void)argv;
    init_scr(); _ps2sdk_init_timer(); SifInitRpc(0);
#ifdef PSBBN_BOOT_BUILD
    report("PSBBN MMCE BOOT EXPERIMENT v0.6.12\nSound disabled; visible early handoff diagnostics enabled.\nRepeated live reads first; Linux boot enables image writes.\n");
#else
    report("PSBBN REAL-IMAGE PREFLIGHT v0.5\nRead-only image; USB checkpoint log writes only.\n");
#endif
    reset_iop();
    if (file_environment() && LOAD(sio2man) && LOAD(mmceman)) {
        for (slot = 0; slot < 2; ++slot) {
            if (read_checks(slot)) { selected = slot; break; }
        }
        result(0, selected >= 0);
        save_report();
        if (selected >= 0) ready = prepare_image(selected);
        if (ready) virtual_tests();
    }
    /* Recover USB logging even after a timed-out diagnostic RPC. */
    reset_iop();
    if (file_environment() && LOAD(sio2man) && LOAD(mmceman) && selected >= 0)
        result(10, read_checks(selected));
    report("\nRESULTS\n");
    for (i = 0; i < 11; ++i)
        report("%s: %s\n", test_names[i], outcomes[i] > 0 ? "PASS" : outcomes[i] < 0 ? "FAIL" : "SKIPPED");
    report("Sample fingerprints detect tested read errors; this is not a full image hash.\n");
#ifndef PSBBN_BOOT_BUILD
    report("No Sony ELF executed. This preflight does not launch Linux.\n");
#endif
    saved = save_report();
    scr_clear(); scr_setXY(0, 0);
#ifdef PSBBN_BOOT_BUILD
    scr_printf("PSBBN MMCE BOOT v0.6.12 - PREFLIGHT FINISHED\n\n");
#else
    scr_printf("PSBBN REAL-IMAGE PREFLIGHT v0.5 - FINISHED\n\n");
#endif
    for (i = 0; i < 11; ++i)
        scr_printf("%-27s %s\n", test_names[i], outcomes[i] > 0 ? "PASS" : outcomes[i] < 0 ? "FAIL" : "SKIPPED");
    scr_printf("\nUSB report: %s\n", saved ? saved_path : "unavailable (photograph results)");
#ifdef PSBBN_BOOT_BUILD
    for (i = 0; i < 11; ++i) if (outcomes[i] != 1) break;
    if (i == 11 && saved && boot_read_handle_closed) launch_psbbn(selected);
    else report("Boot stopped: preflight, handle cleanup and USB checkpoint must pass.\n");
    report("Linux was not entered. Power off to exit.\n");
    if (fxio) save_report();
#else
    scr_printf("\nPSBBN boot not attempted. Power off to exit.\n");
#endif
    SleepThread();
    return 0;
}
