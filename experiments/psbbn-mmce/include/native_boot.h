/* SPDX-License-Identifier: MIT
 * Stages survive SDK startup and IOP reset in a reserved uncached EE page. */
#ifndef PSBBN_NATIVE_BOOT_H
#define PSBBN_NATIVE_BOOT_H
#include <stdint.h>
struct native_boot_state { uint32_t magic, stage, error, argument_count, argument_pointer; };
#ifdef PSBBN_NATIVE_HOST_TEST
static volatile struct native_boot_state host_native_state;
#define NATIVE_BOOT_STATE (&host_native_state)
#else
/* Match PS2SDK UNCACHED_SEG, usable after SetupThread enters application mode.
 * KSEG1 (0xa103d000) is privileged and must not be used by this application. */
#define NATIVE_BOOT_STATE ((volatile struct native_boot_state *)0x2103d000)
#endif
#ifdef PSBBN_PERF_NOW
#define NATIVE_BOOT_NOW() PSBBN_PERF_NOW()
#define NATIVE_BOOT_HZ PSBBN_PERF_HZ
#else
#include <timer.h>
#define NATIVE_BOOT_NOW() GetTimerSystemTime()
#define NATIVE_BOOT_HZ kBUSCLK
#endif
static void native_mark(unsigned int stage, unsigned int error)
{
    NATIVE_BOOT_STATE->magic = 0x324c4250;
    NATIVE_BOOT_STATE->stage = stage;
    NATIVE_BOOT_STATE->error = error;
}
static int native_poll(unsigned int stage, int (*poll)(void))
{
    uint64_t start = NATIVE_BOOT_NOW();
    native_mark(stage, 0);
    do {
        if (poll()) return 1;
    } while (NATIVE_BOOT_NOW() - start < (uint64_t)NATIVE_BOOT_HZ * 20);
    native_mark(stage, 0x100 + stage);
    return 0;
}
#endif
