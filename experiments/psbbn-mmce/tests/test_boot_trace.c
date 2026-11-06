#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PSBBN_TRACE_HOST
#define PSBBN_TRACE_FONT test_font
#include "boot_trace.h"
const unsigned char test_font[256 * 8] = { ['A' * 8] = 128 };
static unsigned int calls, points, fail_at, max_x, max_y;
int psbbn_trace_send(unsigned long long value, unsigned long long reg)
{
    ++calls;
    if (calls == fail_at) return 0;
    if (reg == 5) {
        unsigned int x = (unsigned int)value & 65535;
        unsigned int y = (unsigned int)(value >> 16) & 65535;
        if (x > max_x) max_x = x;
        if (y > max_y) max_y = y;
        ++points;
    }
    return 1;
}
int main(void)
{
    char long_line[100];
    assert(psbbn_trace_row("A", 208));
    assert(points == 3 && max_x == 640 * 16 && max_y == 216 * 16);
    calls = 0;
    assert(!psbbn_trace_row("A", 217) && !calls);
    fail_at = 4;
    assert(!psbbn_trace_row("A", 208) && calls == 4);
    calls = points = fail_at = max_x = max_y = 0;
    memset(long_line, 'A', sizeof(long_line)); /* Deliberately no NUL within the cap. */
    assert(psbbn_trace_row(long_line, 208));
    assert(points == 80 && max_x <= 640 * 16 && max_y <= 224 * 16);
    psbbn_trace("A");
    psbbn_trace_hex("EPC", 0x81001234, 184);
    puts("Early boot trace: clipped output, bounded strings, packet failure and hex rendering passed");
    return 0;
}
