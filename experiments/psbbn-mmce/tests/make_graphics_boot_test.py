"""Compile the patched production graphics boot functions against small mocks."""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()


def function(name):
    match = re.search(r'^(?:static )?int (?:__init )?' + name + r'\([^\n]*\)\n\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


prefix = r'''
#include <assert.h>
#include <stdio.h>
#define __init
#define MAX_NR_CONSOLES 8
#define PS2_GS_NTSC 2
#define PS2_GS_PAL 3
struct ps2_screeninfo { int mode; };
struct ps2_crtmode { int mode, res; };
static struct ps2_screeninfo defaultinfo;
static int graphics_boot, ps2_con, takeovers, mode_calls;
static void take_over_console(const int *con, int first, int last, int deflt)
{ assert(con == &ps2_con && first == 0 && last == 7 && deflt == 1); ++takeovers; }
static void ps2con_initinfo(struct ps2_screeninfo *info) { info->mode = PS2_GS_NTSC; }
static int ps2gs_crtmode(struct ps2_crtmode *mode, void *old)
{ assert(mode->mode == PS2_GS_NTSC && mode->res == -1 && old == NULL); ++mode_calls; return 0; }
'''
main = r'''
int main(void)
{
    assert(graphics_setup("") == 1 && graphics_boot == 1);
    assert(ps2_console_init() == 0 && takeovers == 0 && mode_calls == 1);
    graphics_boot = 0;
    assert(ps2_console_init() == 0 && takeovers == 1 && mode_calls == 1);
    puts("Graphics boot: production handler consumes flag, bypasses native takeover and preserves current mode; text path retained");
    return 0;
}
'''
Path(sys.argv[2]).write_text(prefix + function('graphics_setup') + '\n' + function('ps2_console_init') + '\n' + main)
