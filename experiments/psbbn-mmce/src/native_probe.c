/* SPDX-License-Identifier: GPL-3.0-or-later
 * Direct native-MMCE game test. It never opens or modifies a PSBBN image.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include "native_launch.h"
#include "iso_scan.h"
#include "runtime.h"

#define MOD(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
MOD(iomanX); MOD(fileXio); MOD(sio2man); MOD(mmceman_scan);
#define LOAD(n) load_module(#n, n##_irx, size_##n##_irx)
static struct mmce_catalog catalogs[2] __attribute__((aligned(64)));
static unsigned char io_buffer[32768] __attribute__((aligned(64)));
static char log_text[16384], log_path[96];
static unsigned int log_used;
static int filesystem_ready;
extern char _end[];
#ifdef PSBBN_NATIVE_RECEIVER
#include "native_request.h"
static char target_label[33];
#define NATIVE_UTILITY_BASE 0x100000U
#define NATIVE_RUNTIME_DIRECTORY "native-play"
#define NATIVE_LOG_FILE "PSBBN-PLAY-v01.log"
#else
/* Deliberate target from the user's successful collection log. This is a test
 * utility, not the dashboard receiver; it does not infer a Play selection. */
static const char target_label[] = "PP.MMCE.443548d9.SLUS_207.18";
#define NATIVE_UTILITY_BASE 0x1000000U
#define NATIVE_RUNTIME_DIRECTORY "native-test"
#ifdef PSBBN_PERF_TRACE
#define NATIVE_LOG_FILE "PSBBN-NATIVE-PERF-v01.log"
#else
#define NATIVE_LOG_FILE "PSBBN-NATIVE-TEST-v01.log"
#endif
#endif
static void report(const char *format, ...)
{
    char line[512]; va_list args; unsigned int bytes;
    va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
    /* A restored dashboard leaves different GS/GIF state than a cold ELF
     * launch. The SDK debug renderer resets global DMA and waits without a
     * timeout for GIF completion, including on every printed character.
     * Keep the selected-title receiver independent of that renderer. */
#if !defined(PSBBN_NATIVE_RECEIVER)
    scr_printf("%s", line);
#elif defined(PSBBN_NATIVE_HOST_TEST)
    printf("%s", line);
#endif
    bytes = strlen(line);
    if (bytes < sizeof(log_text) - log_used) { memcpy(log_text + log_used, line, bytes + 1); log_used += bytes; }
}
#include "perf_trace.h"
#include "native_boot.h"
static int read_exact(int fd, void *data, unsigned int size)
{
    unsigned int at = 0; int n;
    while (at < size) {
        n = fileXioRead(fd, (unsigned char *)data + at, size - at);
        if (n <= 0 || (unsigned int)n > size - at) return 0;
        at += n;
    }
    return 1;
}
static int write_exact(int fd, const void *data, unsigned int size)
{
    unsigned int at = 0, chunk; int n;
    while (at < size) {
        chunk = size - at; if (chunk > 4096) chunk = 4096;
        n = fileXioWrite(fd, (const unsigned char *)data + at, chunk);
        if (n <= 0 || (unsigned int)n > chunk) return 0;
        at += n;
    }
    return 1;
}
static int save_report(void)
{
    int fd, ok;
    if (!filesystem_ready || !log_path[0]) return 0;
    fd = fileXioOpen(log_path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return 0;
    ok = write_exact(fd, log_text, log_used);
    if (fileXioClose(fd) < 0) ok = 0;
    return ok;
}
static void stop(const char *why)
{
    if (!NATIVE_BOOT_STATE->error) NATIVE_BOOT_STATE->error = 0x200 + NATIVE_BOOT_STATE->stage;
    report("STOP: %s\n", why); save_report();
    report("Log on MMCE: %s\n", log_path[0] ? log_path : "not available");
    for (;;) SleepThread();
}
static int load_module(const char *name, unsigned char *data, unsigned int size)
{
    int result = -1, id;
    report("Starting native module: %s\n", name);
    id = SifExecModuleBuffer(data, size, 0, NULL, &result);
    report("%s: module=%d start=%d\n", name, id, result);
    return id >= 0 && (result == 0 || result == 2);
}
static int native_reset_request(void) { return SifIopReset("", 0); }
static int native_reset_sync(void) { return SifIopSync(); }
static int cache_load(int slot, int which, struct mmce_catalog *catalog)
{
    char path[96]; s64 bytes; int fd, ok;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/scan-cache-%c.bin", slot, 'a' + which);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0); if (fd < 0) return 0;
    bytes = fileXioLseek64(fd, 0, FIO_SEEK_END);
    ok = bytes >= 32 && bytes <= (s64)sizeof(*catalog) && fileXioLseek64(fd, 0, FIO_SEEK_SET) == 0 &&
        read_exact(fd, catalog, bytes) && mmce_catalog_valid(catalog, bytes, slot);
    if (fileXioClose(fd) < 0) ok = 0;
    return ok;
}
static int iso_read(void *context, uint64_t at, void *data, unsigned int size)
{
    int fd = *(int *)context;
    return fileXioLseek64(fd, at, FIO_SEEK_SET) == (s64)at && read_exact(fd, data, size) ? 0 : -1;
}
static int asset_matches(const char *path, const struct native_runtime_file *asset)
{
    unsigned int at = 0, chunk; int fd, ok;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0); if (fd < 0) return 0;
    ok = 1;
    while (ok && at < asset->size) {
        chunk = asset->size - at; if (chunk > sizeof(io_buffer)) chunk = sizeof(io_buffer);
        ok = read_exact(fd, io_buffer, chunk) && !memcmp(io_buffer, asset->data + at, chunk); at += chunk;
    }
    /* Open starts at zero. Check exact length after comparing the full bytes;
     * the handle closes next, so no return-to-start seek is necessary.
     */
    if (ok) ok = fileXioLseek64(fd, 0, FIO_SEEK_END) == asset->size;
    if (fileXioClose(fd) < 0) ok = 0;
    return ok;
}
static int make_directory(const char *path)
{
    iox_stat_t stat;
    if (fileXioGetStat(path, &stat) >= 0 && FIO_S_ISDIR(stat.mode)) return 1;
    /* An untyped/zero stat must not prevent attempting directory creation. */
    return fileXioMkdir(path, 0777) >= 0 ||
        (fileXioGetStat(path, &stat) >= 0 && FIO_S_ISDIR(stat.mode));
}
static void prepare_runtime(int slot, char directory[96])
{
    unsigned int i, reused = 0, deployed = 0; char path[192]; int fd, ok;
    report("Runtime verification r2: bytes then exact length\n");
    snprintf(directory, 96, "mmce%d:/PSBBN/" NATIVE_RUNTIME_DIRECTORY "-%s", slot, native_runtime_id);
    if (!make_directory(directory)) stop("Cannot create isolated native test runtime");
    snprintf(path, sizeof(path), "%s/config", directory);
    if (!make_directory(path)) stop("Cannot create native config directory");
    snprintf(path, sizeof(path), "%s/modules", directory);
    if (!make_directory(path)) stop("Cannot create native modules directory");
    for (i = 0; i < native_runtime_count; ++i) {
        const struct native_runtime_file *asset = &native_runtime_files[i];
        if (native_crc32(asset->data, asset->size) != asset->crc) stop("Embedded runtime integrity failure");
        snprintf(path, sizeof(path), "%s/%s", directory, asset->path);
        if (!asset_matches(path, asset)) {
            fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
            if (fd < 0) { report("Cannot create %s (%d)\n", path, fd); stop("Runtime deployment failed"); }
            ok = write_exact(fd, asset->data, asset->size);
            if (fileXioClose(fd) < 0) ok = 0;
            if (!ok || !asset_matches(path, asset)) { report("Failed runtime file: %s\n", path); stop("Runtime readback failed"); }
            ++deployed;
        } else ++reused;
        if ((i + 1) % 10 == 0) { report("Runtime files verified: %u/%u\n", i + 1, native_runtime_count); save_report(); }
    }
    report("Verified native runtime %s: %u files\n", native_runtime_id, native_runtime_count);
    perf_count("runtime_files_reused", reused);
    perf_count("runtime_files_deployed", deployed);
}
int main(int argc, char **argv)
{
    int slot, found_slot = -1, a, b, fd, ok;
    unsigned int i;
    uint64_t stage;
    struct mmce_catalog *catalog;
    const struct mmce_catalog_entry *match;
    struct mmce_catalog_entry game;
    struct psbbn_iso_reader reader;
    char directory[96], elf[128], cwd_arg[128], dvd_arg[272], iso[256], id[12];
    char *args[4]; t_ExecData execution;
    unsigned int gate_error = NATIVE_BOOT_STATE->magic == 0x324c4250 ? NATIVE_BOOT_STATE->error : 0;
    native_mark(4, gate_error);
#ifndef PSBBN_NATIVE_RECEIVER
    init_scr(); scr_clear();
#endif
    _ps2sdk_init_timer();
    perf_init("native_mmce");
#ifdef PSBBN_NATIVE_RECEIVER
    report("PSBBN NATIVE MMCE PLAY RECEIVER v0.4 (SDK uncached addresses)\n");
    if (gate_error) { report("Restart gate: error=%08x argc=%u pointer=%08x\n", gate_error,
        NATIVE_BOOT_STATE->argument_count, NATIVE_BOOT_STATE->argument_pointer);
        stop("Dashboard restart arguments were rejected before native entry"); }
    if (argc != 3 || !argv || !argv[0] || !argv[1] || !argv[2] || strcmp(argv[0], "rom0:MBRBOOT") ||
        !native_request_decode((const unsigned char *)(0x20000000U | NATIVE_REQUEST_BASE), argv[1], argv[2], target_label))
        stop("Invalid or unsupported dashboard launch request");
    report("Selected collection entry: %s\n", target_label);
    native_mark(5, 0);
#else
    (void)argc; (void)argv;
    report("PSBBN NATIVE MMCE GAME TEST v0.1\nTarget: Sonic Heroes SLUS_207.18\n");
    report("Direct test from USB; this does not implement the PSBBN Play handoff.\n");
#endif
    stage = perf_begin("native_environment");
    report("Native stage 10: preparing RPC reset\n"); native_mark(10, 0);
    SifExitIopHeap(); SifLoadFileExit(); SifExitRpc(); SifInitRpc(0);
    report("Native stage 11: requesting IOP reset\n");
    if (!native_poll(11, native_reset_request)) stop("IOP reset request timed out after 20 seconds");
    report("Native stage 12: waiting for IOP reset\n");
    if (!native_poll(12, native_reset_sync)) stop("IOP reset synchronization timed out after 20 seconds");
    report("Native stage 13: binding native services\n"); native_mark(13, 0);
    SifInitRpc(0); SifInitIopHeap(); SifLoadFileInit();
    sbv_patch_enable_lmb(); sbv_patch_disable_prefix_check(); sbv_patch_fileio();
    if (!LOAD(iomanX) || !LOAD(fileXio) || !LOAD(sio2man) || !LOAD(mmceman_scan) || fileXioInit() < 0)
        stop("Cannot initialize native MMCE filesystem");
    filesystem_ready = 1;
    native_mark(20, 0);
    perf_end("native_environment", stage, 1);
    stage = perf_begin("selected_game_lookup");
    for (slot = 0; slot < 2; ++slot) {
        a = cache_load(slot, 0, &catalogs[0]); b = cache_load(slot, 1, &catalogs[1]);
        report("MMCE%d scan cache validation: a=%d b=%d\n", slot, a, b);
        if (!a && !b) continue;
#ifdef PSBBN_NATIVE_RECEIVER
        if (!log_path[0]) snprintf(log_path, sizeof(log_path), "mmce%d:/PSBBN/" NATIVE_LOG_FILE, slot);
        if (!save_report()) stop("Cannot save native cache checkpoint on MMCE");
#endif
        catalog = b && (!a || mmce_catalog_newer(catalogs[1].generation, catalogs[0].generation)) ? &catalogs[1] : &catalogs[0];
#ifdef PSBBN_NATIVE_RECEIVER
        if (native_game_match_count(catalog, target_label) > 1) stop("Selected collection entry is duplicated in the scan cache");
#endif
        match = native_find_game(catalog, target_label); if (!match) continue;
        if (found_slot >= 0) stop("Target exists on both MMCE cards; test selection is ambiguous");
        found_slot = slot; game = *match;
        report("Target found on mmce%d: cache generation %u\n", slot, catalog->generation);
        snprintf(log_path, sizeof(log_path), "mmce%d:/PSBBN/" NATIVE_LOG_FILE, slot);
    }
#ifdef PSBBN_NATIVE_RECEIVER
    if (found_slot < 0) stop("Selected collection entry missing or ambiguous in valid scan caches");
#else
    if (found_slot < 0) stop("Sonic Heroes exact collection entry missing from a valid scan cache");
#endif
    perf_end("selected_game_lookup", stage, 1);
    native_mark(30, 0);
    stage = perf_begin("selected_iso_validation");
    snprintf(iso, sizeof(iso), "mmce%d:/%s", found_slot, game.path);
    report("Checking %s\n", iso);
#ifdef PSBBN_NATIVE_RECEIVER
    if (!save_report()) stop("Cannot save selected ISO checkpoint on MMCE");
#endif
    fd = fileXioOpen(iso, FIO_O_RDONLY, 0); if (fd < 0) stop("Cannot open selected ISO");
    reader.context = &fd; reader.read_at = iso_read; reader.bytes = ((uint64_t)game.size_hi << 32) | game.size_lo;
    ok = fileXioLseek64(fd, 0, FIO_SEEK_END) == (s64)reader.bytes && psbbn_iso_game_id(&reader, id) == 0 && !strcmp(id, game.id);
    if (fileXioClose(fd) < 0) ok = 0;
    if (!ok) stop("Selected ISO failed size/SYSTEM.CNF/boot-ELF checks");
    perf_end("selected_iso_validation", stage, 1);
    report("Verified ISO: %s, %u:%08x bytes\n", game.id, game.size_hi, game.size_lo);
    if (!save_report()) stop("Cannot save native ISO checkpoint on MMCE");
    stage = perf_begin("runtime_prepare_verify");
    native_mark(40, 0);
    prepare_runtime(found_slot, directory);
    perf_end("runtime_prepare_verify", stage, 1);
    snprintf(elf, sizeof(elf), "%s/neutrino.elf", directory);
    for (i = 0; i < native_runtime_count && strcmp(native_runtime_files[i].path, "neutrino.elf"); ++i) {}
    if (i == native_runtime_count || !native_elf_valid(native_runtime_files[i].data, native_runtime_files[i].size,
        NATIVE_UTILITY_BASE, (u32)_end + 0x100000))
        stop("Neutrino ELF could overlap the native test utility or EE kernel");
    snprintf(cwd_arg, sizeof(cwd_arg), "-cwd=%s", directory);
    snprintf(dvd_arg, sizeof(dvd_arg), "-dvd=%s", iso);
    args[0] = elf; args[1] = cwd_arg; args[2] = "-bsd=mmce"; args[3] = dvd_arg;
    report("Loading %s\nArguments: %s %s %s\n", elf, cwd_arg, args[2], dvd_arg);
    if (!save_report()) stop("Cannot save pre-launch checkpoint on MMCE");
    memset(&execution, 0, sizeof(execution));
    stage = perf_begin("neutrino_elf_load");
    native_mark(50, 0);
    ok = SifLoadElf(elf, &execution);
    perf_end("neutrino_elf_load", stage, ok >= 0 && execution.epc == native_le32(native_runtime_files[i].data + 24));
    report("Native ELF load: result=%d entry=%08x gp=%08x\n", ok, execution.epc, execution.gp);
    if (ok < 0 || execution.epc != native_le32(native_runtime_files[i].data + 24)) stop("Neutrino load failed");
#ifdef PSBBN_NATIVE_RECEIVER
    report("Checkpoint: executing Neutrino for selected entry %s (%s)\n", target_label, game.id);
#else
    report("Checkpoint: executing Neutrino for the exact Sonic Heroes ISO\n");
#endif
    perf_checkpoint("pre_execps2");
    if (!save_report()) stop("Cannot save final native checkpoint");
    fileXioExit(); filesystem_ready = 0; SifLoadFileExit(); SifExitRpc();
    FlushCache(0); FlushCache(2);
    native_mark(60, 0);
    ExecPS2((void *)execution.epc, (void *)execution.gp, 4, args);
    stop("Native ExecPS2 unexpectedly returned");
    return 0;
}
