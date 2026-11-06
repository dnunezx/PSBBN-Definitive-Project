"""Compile production library lookup/hooks with host IOP mocks.

Usage: python tests/test_sio2man_hook.py build/test_sio2man_hook.c
Add --legacy-selection to reproduce the old first-name-match bug.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
sources = '\n'.join((root / name).read_text() for name in (
    'vendor/mmcefhi/src/ioplib.c', 'vendor/mmcefhi/src/sio2man_hook.c'))
sources = re.sub(r'^#include[^\n]*', '', sources, flags=re.M)
if '--legacy-selection' in sys.argv:
    sources = sources.replace('lib = sio2man_find_library();',
                              'lib = ioplib_getByName("sio2man");')

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define IRX_VER(major, minor) (((major) << 8) | (minor))
#define DPRINTF(...) ((void)0)
typedef uint32_t u32;
typedef uint16_t u16;
struct irx_import_stub { u32 jump; unsigned int fno; };
struct irx_import_table { struct irx_import_table *next; struct irx_import_stub *stubs; };
typedef struct iop_library {
    struct iop_library *prev;
    unsigned int version;
    char name[8];
    void *exports[64];
    struct irx_import_table *caller;
} iop_library_t;
static struct { iop_library_t *let_next; } loadcore;
#define GetLoadcoreInternalData() (&loadcore)
static void CpuSuspendIntr(int *state) { *state = 0; }
static void CpuResumeIntr(int state) { assert(state == 0); }
typedef struct { int attr, initial, max, option; } iop_sema_t;
static int CreateSema(iop_sema_t *sema) { assert(sema->initial == 1); return 1; }
static int DeleteSema(int sema) { assert(sema == 1); return 0; }
static int WaitSema(int sema) { assert(sema == 1); return 0; }
static int SignalSema(int sema) { assert(sema == 1); return 0; }
typedef struct { int unused; } sio2_transfer_data_t;
typedef struct { struct { int (*handler)(void *); void *userdata; } interrupt_handler_table[32]; } intrman_internals_t;
static intrman_internals_t interrupts;
static intrman_internals_t *GetIntrmanInternalData(void) { return &interrupts; }
int (*sio2man_intr_handler_ptr)(void *), (*mmce_sio2_intr_handler_ptr)(void *);
void *sio2man_intr_arg_ptr;
static int init_calls, reset_calls;
static void original_init(void) { ++init_calls; }
static void original_reset(void) { ++reset_calls; }
static int original_transfer(sio2_transfer_data_t *td) { return td != NULL; }
static void setup(iop_library_t *lib, unsigned version, iop_library_t *previous)
{
    unsigned i;
    memset(lib, 0, sizeof(*lib));
    memcpy(lib->name, "sio2man", 8);
    lib->version = version; lib->prev = previous;
    for (i = 0; i < 63; ++i) lib->exports[i] = original_init;
    lib->exports[25] = original_transfer;
    lib->exports[26] = original_reset;
    lib->exports[version < 0x200 ? 49 : 51] = original_transfer;
    lib->exports[version < 0x200 ? 50 : 52] = original_reset;
}
'''
main = r'''
int main(void)
{
    iop_library_t modern, legacy, older;
    struct irx_import_stub stubs[] = {{1, 51}, {0, 0}};
    struct irx_import_table imports = {NULL, stubs};
    unsigned order;
    for (order = 0; order < 2; ++order) {
        setup(&modern, 0x207, NULL);
        setup(&legacy, 0x102, &modern);
        if (order) { modern.prev = &legacy; legacy.prev = NULL; }
        loadcore.let_next = order ? &modern : &legacy;
        modern.caller = &imports;
        assert(ioplib_getByName("sio2man") == (order ? &modern : &legacy));
        assert(ioplib_getByNameVersion("sio2man", 0x203) == &modern);
        assert(sio2man_find_library() == &modern);
        assert(sio2man_hook_init() == 0);
        assert(hooked_library == &modern && hooked_version == 0x207);
        assert(modern.exports[46] == _46_sio2_pad_transfer_init);
        assert(modern.exports[51] == _51_sio2_transfer);
        assert(legacy.exports[46] == original_init);
        assert(stubs[0].jump == (0x08000000 | (((u32)(uintptr_t)_51_sio2_transfer << 4) >> 6)));
        sio2man_hook_deinit();
        assert(!hooked_library && !hooked_version);
        assert(modern.exports[46] == original_init && modern.exports[51] == original_transfer);
        assert(legacy.exports[46] == original_init);
    }
    assert(init_calls == 2 && reset_calls == 2);
    setup(&older, 0x202, &modern); modern.prev = NULL;
    loadcore.let_next = &older;
    assert(ioplib_getByNameVersion("sio2man", 0x203) == &modern);
    assert(ioplib_getByNameVersion("sio2man", 0x208) == NULL);
    assert(ioplib_getByNameVersion("sio2man", 0x300) == NULL);
    assert(ioplib_getByNameVersion("missing", 0x200) == NULL);
    loadcore.let_next = &legacy; legacy.prev = NULL;
    assert(sio2man_find_library() == &legacy);
    assert(sio2man_hook_init() == 0 && hooked_library == &legacy);
    assert(legacy.exports[49] == _51_sio2_transfer);
    sio2man_hook_deinit();
    assert(legacy.exports[49] == original_transfer && legacy.exports[50] == original_reset);
    loadcore.let_next = NULL;
    assert(sio2man_find_library() == NULL);
    puts("SIO2 hooks: both registration orders, modern caller relink/restore, versions and legacy fallback passed");
    return 0;
}
'''
Path(sys.argv[1]).write_text(prefix + sources + main)
