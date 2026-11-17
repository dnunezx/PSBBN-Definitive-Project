/* SPDX-License-Identifier: MIT
 * Linux o32 dashboard hooks: record progress, then call the unchanged routine.
 * No collection selection is changed; failure to log never prevents launch. */
#include <stdint.h>
#include <stddef.h>
extern void bn_reboot_original(void);
extern void bn_restore_original(void);
/* A freestanding non-PIC caller may emit jalr v0. Linux PIC callees require
 * t9 to hold their entry, and unresolved GOT stubs require the bn GP. */
extern int bn_import3(unsigned int, uintptr_t, uintptr_t, uintptr_t);
#define CALL(slot, a, b, c) bn_import3(slot, (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c))

static void write_all(int fd, const void *data, unsigned int length)
{
    const unsigned char *p = data;
    while (length) {
        int n = CALL(0x100d31ac, fd, p, length);
        if (n <= 0 || (unsigned int)n > length) break;
        p += n; length -= n;
    }
}
static void launch_trace(unsigned int stage, const char *text, unsigned int length)
{
    volatile unsigned int *marker = (volatile unsigned int *)(uintptr_t)0x009a0040;
    char buffer[1024]; int fd, input, count;
    marker[0] = 0x32544c42; marker[1] = stage;
    fd = CALL(0x100d8e90, "/opt1/psbbn-launch.log", stage == 1 ? 0x301 : 0x109, 0600);
    marker[2] = (unsigned int)fd;
    if (fd < 0) return;
    write_all(fd, text, length);
    input = CALL(0x100d8e90, "/proc/psbbn_mmce", 0, 0);
    if (input >= 0) {
        count = CALL(0x100dae8c, input, buffer, sizeof(buffer));
        if (count > 0 && count <= (int)sizeof(buffer)) write_all(fd, buffer, (unsigned int)count);
        CALL(0x100d3498, input, 0, 0);
    }
    CALL(0x100d79b8, fd, 0, 0);
    CALL(0x100d3498, fd, 0, 0);
}
void bn_reboot_hook(void)
{
    static const char before[] = "PSBBN launch stage 1: dashboard reboot dispatch\n";
    static const char after[] = "PSBBN launch stage 2: reboot dispatch returned\n";
    launch_trace(1, before, sizeof(before) - 1);
    bn_reboot_original();
    launch_trace(2, after, sizeof(after) - 1);
}
void bn_restore_hook(void)
{
    static const char before[] = "PSBBN launch stage 3: preparing native bootstrap and Linux shutdown\n";
    static const char after[] = "PSBBN launch stage 4: bootstrap preparation/reboot returned unexpectedly\n";
    launch_trace(3, before, sizeof(before) - 1);
    bn_restore_original();
    launch_trace(4, after, sizeof(after) - 1);
}
