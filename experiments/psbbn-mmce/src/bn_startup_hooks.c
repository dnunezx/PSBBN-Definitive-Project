/* Linux o32 startup hooks. Original routines and HTTP request policy remain. */
#include <stdint.h>
extern void bn_ui_original(void);
extern int bn_import3(unsigned int, uintptr_t, uintptr_t, uintptr_t);
#define CALL(slot,a,b,c) bn_import3(slot,(uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c))
static const unsigned int original_rmdir = 0x004a4120;
int bn_cache_init_hook(uintptr_t a, uintptr_t b, uintptr_t c)
{
    int fd = CALL(0x100d8e90, "/var/run/psbbn-cache-validated", 0, 0);
    volatile unsigned int *state = (volatile unsigned int *)0x009a0080;
    if (fd >= 0) {
        CALL(0x100d3498, fd, 0, 0);
        state[0] = 1;
        return 0;
    }
    state[0] = 2;
    return CALL((uintptr_t)&original_rmdir, a, b, c);
}
void bn_ui_ready_hook(void)
{
    volatile unsigned int *state = (volatile unsigned int *)0x009a0080;
    int fd;
    bn_ui_original();
    if (state[1]) return;
    fd = CALL(0x100d8e90, "/var/run/psbbn-ui-ready", 0x301, 0600);
    state[1] = fd >= 0 ? 1 : 2;
    if (fd >= 0) CALL(0x100d3498, fd, 0, 0);
}
