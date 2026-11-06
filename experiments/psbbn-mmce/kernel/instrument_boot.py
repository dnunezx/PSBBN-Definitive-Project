"""Add visible entry/CPU/PROM markers to the pinned kernel, failing on drift."""
from pathlib import Path
import difflib
import sys

root = Path(sys.argv[1])
originals, modified = {}, {}


def edit(relative, old, new):
    path = root / relative
    if relative not in originals:
        originals[relative] = path.read_text()
    text = modified.get(relative, originals[relative])
    if text.count(old) != 1:
        raise ValueError(f'Expected one instrumentation anchor in {relative}: {old!r}')
    modified[relative] = text.replace(old, new)


edit('arch/mips/ps2/Makefile', 'include $(TOPDIR)/Rules.make',
     'obj-$(CONFIG_PSBBN_MMCE) += boot_trace.o boot_trace_font.o\n\ninclude $(TOPDIR)/Rules.make')
edit('arch/mips/kernel/head.S', '\tjal\tinit_arch\n\t nop', '''\t/* Preserve boot arguments across the screen-only entry marker. */
\taddiu\tsp, sp, -32
\tsw\ta0, 16(sp)
\tsw\ta1, 20(sp)
\tsw\ta2, 24(sp)
\tsw\ta3, 28(sp)
\tjal\tpsbbn_trace_entry
\t nop
\tlw\ta0, 16(sp)
\tlw\ta1, 20(sp)
\tlw\ta2, 24(sp)
\tlw\ta3, 28(sp)
\taddiu\tsp, sp, 32
\tjal\tinit_arch
\t nop''')
include = '''#ifdef CONFIG_PSBBN_MMCE
#define PSBBN_TRACE_FONT psbbn_trace_font
#include <asm/ps2/boot_trace.h>
extern void psbbn_early_console_init(void);
extern void psbbn_early_console_stop(void);
#endif
'''
edit('arch/mips/kernel/setup.c', '#include <linux/init.h>', '#include <linux/init.h>\n' + include)
edit('arch/mips/kernel/setup.c', '\tcpu_probe();', '''#ifdef CONFIG_PSBBN_MMCE
\tpsbbn_trace("K1 LINUX CPU PROBE");
#endif
\tcpu_probe();
#ifdef CONFIG_PSBBN_MMCE
\tpsbbn_trace("K2 LINUX CPU PROBE FINISHED");
#endif''')
edit('arch/mips/kernel/setup.c', '\tstart_kernel();', '''#ifdef CONFIG_PSBBN_MMCE
\tpsbbn_early_console_init();
\tpsbbn_trace("K5 LINUX START_KERNEL");
#endif
\tstart_kernel();''')
edit('arch/mips/ps2/prom.c', '#include "ps2.h"', '#include "ps2.h"\n' + include)
edit('arch/mips/ps2/prom.c', '\t/* default bootinfo */', '''\tpsbbn_trace("K3 LINUX PROM PARAMETERS");
\t/* default bootinfo */''')
edit('arch/mips/ps2/prom.c', '\tadd_memory_region(0, ps2_bootinfo->maxmem & PAGE_MASK, BOOT_MEM_RAM);',
     '\tadd_memory_region(0, ps2_bootinfo->maxmem & PAGE_MASK, BOOT_MEM_RAM);\n'
     '\tpsbbn_trace("K4 LINUX BIOS VALIDATED");')
edit('drivers/video/ps2con.c', '#undef PS2CONDEBUG',
     '#undef PS2CONDEBUG\nextern void psbbn_early_console_stop(void);')
edit('drivers/video/ps2con.c', '    ps2con_initinfo(&defaultinfo);', '''#ifdef CONFIG_PSBBN_MMCE
    psbbn_early_console_stop();
#endif
    ps2con_initinfo(&defaultinfo);''')
# checksetup() consumes only a nonzero return. Do not pass "graphics" on as
# an unexpected argv entry to /sbin/init after selecting graphics boot.
edit('drivers/video/ps2con.c', '    graphics_boot = 1;\n    return 0;',
     '    graphics_boot = 1;\n    return 1;')
edit('drivers/ps2/ps2dev.c', '#include <linux/config.h>',
     '#include <linux/config.h>\n#ifdef CONFIG_PSBBN_MMCE\n'
     'extern void psbbn_early_console_graphics_handoff(void);\n#endif')
edit('drivers/ps2/ps2dev.c', '''    file->private_data = dev;
    MOD_INC_USE_COUNT;
    return 0;
}

static int ps2gs_release''', '''    file->private_data = dev;
    MOD_INC_USE_COUNT;
#ifdef CONFIG_PSBBN_MMCE
    psbbn_early_console_graphics_handoff();
#endif
    return 0;
}

static int ps2gs_release''')
# Keep each existing initcall slot and order, wrapping its original function.
# printk stays outside init.h so headers need no new include dependencies.
old_initcall = '#define __initcall(fn)' + '\t' * 8 + '\\\n\tstatic initcall_t __initcall_##fn __init_call = fn'
new_initcall = '''#ifdef CONFIG_PSBBN_MMCE
extern int psbbn_run_initcall(initcall_t function, const char *name);
#define __initcall(fn) \\
    static int __init psbbn_initcall_##fn(void) \\
    { return psbbn_run_initcall(fn, #fn); } \\
    static initcall_t __initcall_##fn __init_call = psbbn_initcall_##fn
#else
''' + old_initcall + '\n#endif'
edit('include/linux/init.h', old_initcall, new_initcall)


def progress(relative, call, label):
    edit(relative, '\t' + call, '#ifdef CONFIG_PSBBN_MMCE\n'
         '\tprintk("PSBBN: ' + label + '\\n");\n#endif\n\t' + call)


for call, label in [('setup_arch(&command_line);', 'architecture setup'),
                    ('trap_init();', 'exception setup'),
                    ('init_IRQ();', 'interrupt setup'),
                    ('sched_init();', 'scheduler setup'),
                    ('time_init();', 'timer setup'),
                    ('console_init();', 'native console setup'),
                    ('calibrate_delay();', 'delay calibration'),
                    ('mem_init();', 'memory initialization')]:
    progress('init/main.c', call, label)
for call, label in [('ps2dma_init();', 'PS2 DMA initialization'),
                    ('ps2sif_init();', 'PS2 SIF initialization'),
                    ('ps2rtc_init();', 'PS2 RTC initialization'),
                    ('ps2_powerbutton_init();', 'PS2 power initialization')]:
    progress('arch/mips/ps2/setup.c', call, label)
progress('drivers/char/mem.c', 'ps2_console_init();', 'native PS2 text console')
patch = ''.join(''.join(difflib.unified_diff(originals[name].splitlines(True),
                text.splitlines(True), fromfile='a/' + name, tofile='b/' + name))
                for name, text in modified.items())
Path(sys.argv[2]).write_text(patch)
print('Generated kernel entry, CPU, PROM and start_kernel marker patch')
