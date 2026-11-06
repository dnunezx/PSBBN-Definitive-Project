/* SPDX-License-Identifier: MIT
 * PCSX2-only launcher: native virtual DEV9 HDD -> existing block RPC/kernel.
 * Run against a disposable copy; intentionally separate from MMCE preflight.
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
#include "real_checks.h"
#define PSBBN_EMULATOR_BUILD
#define MOD(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
#define LOAD(n) module(#n, n##_irx, size_##n##_irx)
MOD(sio2man); MOD(native_dev9); MOD(native_atad);
extern unsigned char emulator_checks[];
extern unsigned int size_emulator_checks;
static struct real_checks checks __attribute__((aligned(64)));
static SifRpcClientData_t rpc __attribute__((aligned(64)));
static int emulator_stdout = 1;
static void report(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    scr_printf("%s", line);
    if (emulator_stdout) printf("%s", line);
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
    SifExitIopHeap(); SifLoadFileExit(); SifExitRpc(); SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0); SifInitIopHeap(); SifLoadFileInit();
    sbv_patch_enable_lmb();
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
static unsigned int crc32(const unsigned char *p, unsigned int size)
{ return ~real_crc_update(~0U, p, size); }
/* Error recovery shared with the hardware launcher; emulator logs use stdout. */
static int file_environment(void) { return 1; }
static int save_report(void) { return 1; }
#include "boot_launch.h"
int main(void)
{
    init_scr(); _ps2sdk_init_timer(); SifInitRpc(0);
    report("PCSX2 test: embedded profile validation\n");
    if (size_emulator_checks > sizeof(checks)) {
        report("Profile too large\n"); SleepThread(); return 1;
    }
    memcpy(&checks, emulator_checks, size_emulator_checks);
    if (!real_checks_valid(&checks, size_emulator_checks)) {
        report("Invalid profile\n"); SleepThread(); return 1;
    }
    launch_psbbn(0);
    report("Linux was not entered. Inspect PCSX2 log.\n");
    SleepThread();
    return 0;
}
