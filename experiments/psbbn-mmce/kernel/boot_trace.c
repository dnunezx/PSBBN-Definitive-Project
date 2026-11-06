/* SPDX-License-Identifier: MIT */
#include <linux/console.h>
#include <linux/kernel.h>
#define PSBBN_TRACE_FONT psbbn_trace_font
#include <asm/ps2/boot_trace.h>

static int early_active, early_registered;
static unsigned int early_row, early_used, early_sequence;
static char early_line[79];

static void early_new_line(void)
{
    unsigned int i, digit;
    for (i = 0; i < 4; ++i) {
        digit = (early_sequence >> (12 - i * 4)) & 15;
        early_line[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
    }
    early_line[4] = ' ';
    early_used = 5;
    early_line[early_used] = 0;
}

static void early_render(void)
{
    early_line[early_used] = 0;
    if (!psbbn_trace_row(early_line, 8 + early_row * 8)) early_active = 0;
}

static void early_write(struct console *console, const char *text, unsigned int count)
{
    unsigned int i;
    char c;
    (void)console;
    if (!early_active) return;
    for (i = 0; i < count && early_active; ++i) {
        c = text[i];
        if (c == '\r') { early_used = 5; continue; }
        if (c == '\n' || early_used == 78) {
            early_render();
            early_row = (early_row + 1) % 18;
            ++early_sequence;
            early_new_line();
            if (c == '\n') continue;
        }
        if (c == '\t') c = ' ';
        if ((unsigned char)c >= 32) early_line[early_used++] = c;
    }
    /* Show partial lines too: a driver may hang before printing its newline. */
    if (early_active && early_used > 5) early_render();
}

static struct console early_console = {
    name: "psbbn",
    write: early_write,
    flags: CON_ENABLED | CON_PRINTBUFFER,
    index: -1
};

void psbbn_early_console_init(void)
{
    unsigned int row;
    if (early_registered) return;
    early_registered = 1;
    early_active = 1;
    early_new_line();
    for (row = 0; row < 20 && early_active; ++row)
        if (!psbbn_trace_row("", row * 8)) early_active = 0;
    if (!early_active) return;
    (void)psbbn_trace_row("PSBBN EARLY LINUX LOG - numbered lines wrap", 0);
    register_console(&early_console);
}

void psbbn_early_console_stop(void)
{
    /* Native console startup may hold console_sem. Do not unregister/relock
     * here; leave a silent callback once Linux owns the GS and GIF channel.
     */
    if (early_active) psbbn_trace("K6 NATIVE GS CONSOLE TAKEOVER");
    early_active = 0;
}

void psbbn_early_console_graphics_handoff(void)
{
    if (early_active) psbbn_trace("K7 USERSPACE GRAPHICS DEVICE OPENED");
    early_active = 0;
}

int psbbn_run_initcall(int (*function)(void), const char *name)
{
    int result;
    printk("PSBBN init: %s\n", name);
    result = function();
    printk("PSBBN done: %s = %d\n", name, result);
    return result;
}

void psbbn_trace_entry(void)
{
    psbbn_trace("K0 LINUX ENTRY REACHED");
}
