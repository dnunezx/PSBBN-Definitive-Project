#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../kernel/boot_trace.c"
static char screen[28][79];
static unsigned int renders, registrations, fail_render;
static struct console *registered;
static char event_log[256];
static unsigned int event_used, initializer_calls;
int printk(const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = vsnprintf(event_log + event_used, sizeof(event_log) - event_used, format, args);
    va_end(args);
    assert(result >= 0 && (unsigned int)result < sizeof(event_log) - event_used);
    event_used += result;
    return result;
}
static int initializer(void) { ++initializer_calls; return -17; }
int psbbn_trace_row(const char *text, unsigned int row)
{
    assert(row % 8 == 0 && row < 224 && strlen(text) <= 78);
    ++renders;
    if (fail_render) return 0;
    strcpy(screen[row / 8], text);
    return 1;
}
void register_console(struct console *console) { registered = console; ++registrations; }
int main(void)
{
    char large[1200];
    unsigned int before;
    psbbn_early_console_init(); psbbn_early_console_init();
    assert(registrations == 1 && registered);
    registered->write(registered, "partial", 7);
    assert(!strcmp(screen[1], "0000 partial"));
    registered->write(registered, " line\nnext", 10);
    assert(!strcmp(screen[1], "0000 partial line") && !strcmp(screen[2], "0001 next"));
    registered->write(registered, "\rnew", 4);
    assert(!strcmp(screen[2], "0001 new"));
    memset(large, 'X', sizeof(large));
    registered->write(registered, large, sizeof(large));
    assert(early_used <= 78 && early_row < 18);
    registered->write(registered, "\n1\n2\n3\n", 7);
    assert(early_row < 18);
    psbbn_early_console_stop(); before = renders;
    registered->write(registered, "silent", 6);
    assert(renders == before);
    early_active = 1;
    psbbn_early_console_graphics_handoff();
    assert(!early_active && !strcmp(screen[20], "K7 USERSPACE GRAPHICS DEVICE OPENED"));
    before = renders;
    registered->write(registered, "silent after graphics open", 26);
    assert(renders == before);
    early_active = 1; fail_render = 1;
    registered->write(registered, "failed", 6);
    assert(!early_active);
    assert(psbbn_run_initcall(initializer, "mock_driver") == -17);
    assert(initializer_calls == 1);
    assert(!strcmp(event_log, "PSBBN init: mock_driver\nPSBBN done: mock_driver = -17\n"));
    puts("Early Linux console: text bounds/wrap, partial lines, render failure, native-console stop and initcall exactly-once/result checks passed");
    return 0;
}
