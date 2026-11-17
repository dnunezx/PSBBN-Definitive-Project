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
#ifdef PSBBN_USB_BUILD
#include "usb_fhi.h"
#else
#include "fhi_fileid_config.h"
#endif
#include "real_checks.h"
#if defined(PSBBN_SOUND_BUILD) && !defined(PSBBN_COMBINED_BUILD)
#define PSBBN_COMBINED_BUILD
#endif

#define MOD(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
MOD(iomanX); MOD(fileXio); MOD(sio2man);
#if defined(PSBBN_COMBINED_BUILD) && !defined(PSBBN_SOUND_BUILD)
MOD(mmceman_scan);
#define mmceman_irx mmceman_scan_irx
#define size_mmceman_irx size_mmceman_scan_irx
#else
MOD(mmceman);
#endif
MOD(mmcefhi); MOD(atad_emu); MOD(suite_rpc);
MOD(bdm); MOD(bdmfs_fatfs); MOD(usbd); MOD(usbmass_bd);
#ifdef PSBBN_USB_BUILD
MOD(usb_fhi);
#define STORAGE_NAME "USB"
#define BOOT_TITLE "PSBBN USB BOOT v0.1"
static int selected_usb_device = -1;
#else
#define STORAGE_NAME "MMCE"
#ifdef PSBBN_SOUND_BUILD
#define BOOT_TITLE "PSBBN MMCE SOUND TEST v0.1"
#elif defined(PSBBN_COMBINED_BUILD)
#ifdef PSBBN_FAST_BOOT
#define BOOT_TITLE "PSBBN MMCE COMBINED v0.6 / streamlined startup"
#else
#define BOOT_TITLE "PSBBN MMCE COMBINED v0.3 / launch+IO v0.2"
#endif
#else
#define BOOT_TITLE "PSBBN MMCE BOOT v0.6.12"
#endif
#endif
#define LOAD(n) module(#n, n##_irx, size_##n##_irx)
static void storage_wait(unsigned int microseconds)
{
#ifdef PSBBN_USB_BUILD
    /* Poll the EE clock without scheduling a timer-alarm thread. This also
       keeps IOP USB/RPC work progressing on emulator timer implementations. */
    u32 start = cpu_ticks();
    u32 ticks = (u64)microseconds * 294912 / 1000;
    while ((u32)(cpu_ticks() - start) < ticks)
        __asm__ __volatile__("nop");
#else
    usleep(microseconds);
#endif
}
static char log_text[262144], saved_path[80];
static unsigned int log_used;
static int fxio, usb_loaded, rpc_dead;
#if defined(PSBBN_FAST_BOOT) && !defined(PSBBN_USB_BUILD)
static int checkpoint_slot = -1;
#endif
#ifdef PSBBN_BOOT_BUILD
static int boot_read_handle_closed;
#ifdef PSBBN_FAST_BOOT
static unsigned int prepared_profile_crc;
#endif
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

#ifdef PSBBN_FAST_BOOT
static void log_only(const char *format, ...)
{
    char line[512]; va_list args; unsigned int size;
    va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
    size = strlen(line);
    if (size < sizeof(log_text) - log_used) {
        memcpy(log_text + log_used, line, size + 1); log_used += size;
    }
}
#endif


#include "perf_trace.h"

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
        storage_wait(50000);
    }
    report("RPC service %08x missing after 5s\n", id);
    return 0;
}

static int file_environment(void)
{
    if (fxio) return 1;
    if (!LOAD(iomanX) || !LOAD(fileXio) || !bind_service(FILEXIO_IRX)) return 0;
    if (fileXioInit() < 0) return 0;
    fxio = 1;
    return 1;
}

static int start_usb(void)
{
    if (usb_loaded) return 1;
    if (!LOAD(bdm) || !LOAD(bdmfs_fatfs) || !LOAD(usbd) || !LOAD(usbmass_bd)) return 0;
    storage_wait(2000000);
    usb_loaded = 1;
    return 1;
}

static int write_report_path(const char *path)
{
    unsigned int written = 0, chunk; int fd, rv, closed;
    fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return 0;
    while (written < log_used) {
        chunk = log_used - written; if (chunk > 4096) chunk = 4096;
        rv = fileXioWrite(fd, log_text + written, chunk);
        if (rv <= 0 || (unsigned int)rv > chunk) break;
        written += rv;
    }
    closed = fileXioClose(fd);
    if (written != log_used || closed < 0) return 0;
    snprintf(saved_path, sizeof(saved_path), "%s", path);
    return 1;
}

static int save_report(void)
{
#if defined(PSBBN_FAST_BOOT) && !defined(PSBBN_USB_BUILD)
    char card_path[80];
    if (fxio && checkpoint_slot >= 0 && checkpoint_slot < 2) {
        snprintf(card_path, sizeof(card_path), "mmce%d:/PSBBN/PSBBN-COMBINED-v06.log", checkpoint_slot);
        if (write_report_path(card_path)) return 1;
        report("Card checkpoint unavailable; trying USB fallback\n");
    }
#endif
#ifdef PSBBN_USB_BUILD
    char preferred[80];
    const char *paths[] = {preferred, "mass0:/PSBBN-USB-v01.log", "mass1:/PSBBN-USB-v01.log", "mass:/PSBBN-USB-v01.log"};
    snprintf(preferred, sizeof(preferred), "mass%d:/PSBBN-USB-v01.log",
             selected_usb_device < 0 ? 0 : selected_usb_device);
#elif defined(PSBBN_SOUND_BUILD)
    static const char *paths[] = {"mass0:/PSBBN-SOUND-v01.log", "mass1:/PSBBN-SOUND-v01.log", "mass:/PSBBN-SOUND-v01.log"};
#elif defined(PSBBN_COMBINED_BUILD)
#if defined(PSBBN_PERF_TRACE) && !defined(PSBBN_FAST_BOOT)
    static const char *paths[] = {"mass0:/PSBBN-MMCE-PERF-v01.log", "mass1:/PSBBN-MMCE-PERF-v01.log", "mass:/PSBBN-MMCE-PERF-v01.log"};
#else
    static const char *paths[] = {"mass0:/PSBBN-COMBINED-v03.log", "mass1:/PSBBN-COMBINED-v03.log", "mass:/PSBBN-COMBINED-v03.log"};
#endif
#elif defined(PSBBN_BOOT_BUILD)
    static const char *paths[] = {"mass0:/PSBBN-BOOT-v0612.log", "mass1:/PSBBN-BOOT-v0612.log", "mass:/PSBBN-BOOT-v0612.log"};
#else
    static const char *paths[] = {"mass0:/PSBBN-REAL-v05.log", "mass1:/PSBBN-REAL-v05.log", "mass:/PSBBN-REAL-v05.log"};
#endif
    int i;
    if (!fxio || !start_usb()) return 0;
    for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        if (write_report_path(paths[i])) return 1;
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
    int rv, i, limit = (function == 10 || function == 11) ? 180000 : 10000;
    request.lba = lba; request.count = count;
    if (rpc_dead) return 0;
    rv = SifCallRpc(&rpc, function, SIF_RPC_M_NOWAIT, &request, sizeof(request), &reply, sizeof(reply), NULL, NULL);
    if (rv < 0) { report("RPC call failed: %d\n", rv); rpc_dead = 1; return 0; }
    for (i = 0; i < limit && SifCheckStatRpc(&rpc); ++i) storage_wait(1000);
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
    "Read-only PFS mount", "Complete osdboot.elf", "Complete vmlinux", STORAGE_NAME " restored"};
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

static void image_path(char *path, unsigned int capacity, int slot, const char *file)
{
#ifdef PSBBN_USB_BUILD
    snprintf(path, capacity, "mass%d:/PSBBN/%s", slot, file);
#else
    snprintf(path, capacity, "mmce%d:/PSBBN/%s", slot, file);
#endif
}

static int storage_environment(void)
{
#ifdef PSBBN_USB_BUILD
    return start_usb();
#else
    return LOAD(sio2man) && LOAD(mmceman);
#endif
}

#ifdef PSBBN_USB_BUILD
static int patch_usb_settings(int slot, int writable)
{
    struct usb_fhi_settings *settings = NULL;
    unsigned int i;
    for (i = 0; i + sizeof(*settings) <= size_usb_fhi_irx; i += 4) {
        if (*(u32 *)(usb_fhi_irx + i) == USB_FHI_MAGIC) {
            if (settings) return 0;
            settings = (struct usb_fhi_settings *)(usb_fhi_irx + i);
        }
    }
    if (!settings || slot < 0 || slot > 9) return 0;
    memset(settings, 0, sizeof(*settings));
    settings->magic = USB_FHI_MAGIC;
    settings->version = USB_FHI_VERSION;
    settings->writable = writable;
    settings->image_size = checks.image_size;
    image_path(settings->path, sizeof(settings->path), slot, "psbbn.img");
    return 1;
}
#endif

static int read_checks(int slot)
{
    char path[80];
    int fd, pass = 0;
    s64 size;
    image_path(path, sizeof(path), slot, "real-checks.bin");
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
#ifdef PSBBN_FAST_BOOT
        log_only("Sample %u/%u route=%d LBA=%u count=%u CRC=%08x\n",
#else
        report("Sample %u/%u route=%d LBA=%u count=%u CRC=%08x\n",
#endif
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
#ifdef PSBBN_FAST_BOOT
                log_only("Live sample differs from original reference (%08x); repeated read matches.\n", s->crc);
#else
                report("Live sample differs from original reference (%08x); repeated read matches.\n", s->crc);
#endif
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
#ifdef PSBBN_FAST_BOOT
        if ((i + 1) % 16 == 0 || i + 1 == checks.sample_count)
            report("Image sample checks: %u/%u passed\n", i + 1, checks.sample_count);
#endif
    }
    return 1;
}

static int checkpoint_environment(int slot, int reuse)
{
    if (!reuse) {
        reset_iop();
        if (!file_environment() || !storage_environment()) return 0;
    }
    return slot >= 0 && read_checks(slot);
}

static int prepare_image(int slot)
{
    char path[80];
    int fd, pass;
#if !defined(PSBBN_USB_BUILD) && !defined(PSBBN_FAST_BOOT)
    int id;
    unsigned int i;
    struct fhi_fileid *settings = NULL;
#endif
    s64 size;
    image_path(path, sizeof(path), slot, "psbbn.img");
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
#ifdef PSBBN_FAST_BOOT
    /* The final writable bridge independently rereads every learned sample.
     * Keep repeated FILEXIO references, but no persistent diagnostic handle. */
    prepared_profile_crc = checks.body_crc;
    boot_read_handle_closed = fileXioClose(fd) >= 0;
    return boot_read_handle_closed;
#elif defined(PSBBN_USB_BUILD)
    if (fileXioClose(fd) < 0) return 0;
    return patch_usb_settings(slot, 0);
#else
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
#endif
}

#ifndef PSBBN_FAST_BOOT
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
#endif

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

#ifndef PSBBN_FAST_BOOT
static void virtual_tests(void)
{
    int pass, rv;
    report("Checkpoint: resetting into read-only virtual ATA\n");
    save_report();
    reset_iop();
#ifdef PSBBN_USB_BUILD
    if (!file_environment() || !start_usb() || !LOAD(usb_fhi) ||
        !LOAD(atad_ro) || !LOAD(suite_rpc) || !bind_service(SUITE_RPC_ID)) {
#else
    if (!LOAD(sio2man) || !LOAD(mmcefhi) || !LOAD(atad_ro) || !LOAD(suite_rpc) || !bind_service(SUITE_RPC_ID)) {
#endif
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
#endif

#ifdef PSBBN_FAST_BOOT
static int fast_boot_ready(void)
{
    return outcomes[0] == 1 && outcomes[1] == 1 && outcomes[2] == 1 && outcomes[10] == 1 &&
           boot_read_handle_closed && !rpc_dead && prepared_profile_crc == checks.body_crc;
}
#endif

#ifdef PSBBN_BOOT_BUILD
#if defined(PSBBN_COMBINED_BUILD) && !defined(PSBBN_EMULATOR_BUILD)
#include "mmce_scan.h"
#include "collection_sync.h"
#endif
#include "boot_launch.h"
#endif

int main(int argc, char **argv)
{
    int slot, selected = -1, ready = 0, i, saved;
    uint64_t stage;
    (void)argc; (void)argv;
    init_scr(); _ps2sdk_init_timer(); SifInitRpc(0);
    perf_init("mmce_boot");
#ifdef PSBBN_PERF_TRACE
    report("PSBBN MMCE PERFORMANCE v0.3 - ownership snapshot r1; sorted inventory lookup\n");
#endif
#ifdef PSBBN_BOOT_BUILD
#ifdef PSBBN_SOUND_BUILD
    report(BOOT_TITLE "\nSound enabled; visible early handoff diagnostics enabled.\nRepeated live reads first; Linux boot enables image writes.\n");
#else
    report(BOOT_TITLE "\nSound disabled; visible early handoff diagnostics enabled.\nRepeated live reads first; Linux boot enables image writes.\n");
#endif
#else
    report("PSBBN REAL-IMAGE PREFLIGHT v0.5\nRead-only image; USB checkpoint log writes only.\n");
#endif
    stage = perf_begin("initial_environment");
    reset_iop();
    if (file_environment() && storage_environment()) {
#ifdef PSBBN_USB_BUILD
        int attempt;
        report("Searching USB for PSBBN/real-checks.bin (up to 10 seconds)...\n");
        for (attempt = 0; attempt < 100 && selected < 0; ++attempt) {
            for (slot = 0; slot < 10; ++slot) {
                if (read_checks(slot)) { selected = slot; selected_usb_device = slot; break; }
            }
            if (selected < 0) storage_wait(100000);
        }
        if (selected < 0)
            report("No valid USB PSBBN/real-checks.bin found. Copy the full PSBBN folder to the exFAT USB drive.\n");
#else
        for (slot = 0; slot < 2; ++slot) {
            if (read_checks(slot)) { selected = slot; break; }
        }
#endif
        result(0, selected >= 0);
#if defined(PSBBN_FAST_BOOT) && !defined(PSBBN_USB_BUILD)
        checkpoint_slot = selected;
#endif
        perf_end("initial_environment", stage, selected >= 0);
        save_report();
        if (selected >= 0) {
#if defined(PSBBN_COMBINED_BUILD) && !defined(PSBBN_EMULATOR_BUILD)
            int sync_ok;
            stage = perf_begin("scan");
            scan_mmce_games(selected);
            perf_end("scan", stage, scan_is_complete);
            stage = perf_begin("collection_sync");
            sync_ok = sync_collection(selected, scan_is_complete);
            perf_end("collection_sync", stage, sync_ok);
            if (!sync_ok) {
                report("Collection recovery/commit failed; Linux will not start\n"); save_report();
                for (;;) storage_wait(1000000);
            }
            stage = perf_begin("restore_and_image_preflight");
            reset_iop(); rpc_dead = 0;
            if (!file_environment() || !storage_environment() || !read_checks(selected)) {
                report("Cannot restore storage after collection sync\n");
            } else ready = prepare_image(selected);
            perf_end("restore_and_image_preflight", stage, ready);
#else
            ready = prepare_image(selected);
#endif
        }
        if (ready) {
#ifdef PSBBN_FAST_BOOT
            report("Normal boot: repeated FILEXIO references retained; full diagnostic route/file tests omitted.\n");
            report("Every sample will be checked again through the final writable boot bridge.\n");
#else
            stage = perf_begin("virtual_storage_preflight");
            virtual_tests();
            for (i = 0; i < 10 && outcomes[i] == 1; ++i) {}
            perf_end("virtual_storage_preflight", stage, i == 10);
#endif
        }
    } else perf_end("initial_environment", stage, 0);
    /* Recover USB logging even after a timed-out diagnostic RPC. */
    stage = perf_begin("checkpoint_environment");
#ifdef PSBBN_FAST_BOOT
    /* The read-only handle is closed; retain the live filesystem environment
     * for its checkpoint. A failed preparation never proceeds to Linux. */
    result(10, checkpoint_environment(selected, ready && boot_read_handle_closed && !rpc_dead));
#else
    result(10, checkpoint_environment(selected, 0));
#endif
    perf_end("checkpoint_environment", stage, outcomes[10] == 1);
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
    scr_printf(BOOT_TITLE " - PREFLIGHT FINISHED\n\n");
#else
    scr_printf("PSBBN REAL-IMAGE PREFLIGHT v0.5 - FINISHED\n\n");
#endif
#ifdef PSBBN_FAST_BOOT
    if (fast_boot_ready() && saved) scr_printf("Image checks passed. Starting PSBBN.\n");
    else
#endif
    {
        for (i = 0; i < 11; ++i)
            scr_printf("%-27s %s\n", test_names[i], outcomes[i] > 0 ? "PASS" : outcomes[i] < 0 ? "FAIL" : "SKIPPED");
    }
    scr_printf("\nCheckpoint report: %s\n", saved ? saved_path : "unavailable (photograph results)");
#ifdef PSBBN_BOOT_BUILD
#ifdef PSBBN_FAST_BOOT
    if (fast_boot_ready() && saved) {
#else
    for (i = 0; i < 11; ++i) if (outcomes[i] != 1) break;
    if (i == 11 && saved && boot_read_handle_closed) {
#endif
        if (saved) launch_psbbn(selected);
        else report("Boot stopped: game-scan checkpoint could not be saved\n");
    }
    else report("Boot stopped: preflight, handle cleanup and checkpoint must pass.\n");
    report("Linux was not entered. Power off to exit.\n");
    if (fxio) save_report();
#else
    scr_printf("\nPSBBN boot not attempted. Power off to exit.\n");
#endif
    SleepThread();
    return 0;
}
