"""Exercise production SBIOS config callbacks with guarded Linux-sized buffers."""
from pathlib import Path
import re
import subprocess
import tempfile
import os
import sys

root = Path(__file__).resolve().parents[1]
source = (root / 'vendor/tge/sbios/scmd.c').read_text()
if '--mutate-overcopy' in sys.argv:
    source = source.replace('UNCACHED_SEG(sCmdCfgRecvBuff + 8), configReadBytes);',
                            'UNCACHED_SEG(sCmdCfgRecvBuff + 8), 0x400);')

def function(name):
    match = re.search(r'^(?:static )?(?:void|int) ' + name + r'\([^\n]*\)\n\{', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef struct { u32 device, mode, block; void *data; int status; } tge_sbcall_cdvdconfig_arg_t;
typedef struct { void *sbarg; int result; void (*endfunc)(void *, int); void *efarg; } tge_sbcall_rpc_arg_t;
#define UNCACHED_SEG(p) (p)
#define CDVD_UNLOCKS() ((void)0)
#define CD_SCMD_OPENCFG 14
#define CD_SCMD_CLOSECFG 15
#define CD_SCMD_READCFG 16
#define SIF_RPCE_GETP 2
#define SIF_RPCE_SENDP 1
#define SCMD_EAGAIN -1
#define SIF_RPC_M_NOWAIT 1
#define CD_CHECK_SCMD(cmd) (sCmdNum = (cmd), 1)
static unsigned char sCmdRecvBuff[64], sCmdCfgRecvBuff[0x408], sCmdSendBuff[64];
static u32 configReadBytes;
static int sCmdNum, clientSCmd, completed;
static void done(void *unused, int result) { (void)unused; (void)result; ++completed; }
static void SifWriteBackDCache(void *p, int n) { (void)p; (void)n; }
static int SifCallRpc(void *client, int cmd, int mode, void *send, int size,
                     void *recv, int n, void (*callback)(void *), void *arg)
{
    (void)client; (void)cmd; (void)mode; (void)send; (void)size; (void)recv; (void)n;
    callback(arg);
    return 0;
}
'''
main = r'''
int main(void)
{
    unsigned char guarded[15 * 68 + 32];
    unsigned int blocks, bytes, i;
    tge_sbcall_cdvdconfig_arg_t arg = {1, 0, 3, guarded + 16, 0};
    tge_sbcall_rpc_arg_t call = {&arg, 0, done, 0};
    *(s32 *)sCmdRecvBuff = 1;
    *(s32 *)sCmdCfgRecvBuff = 1;
    memset(sCmdCfgRecvBuff + 8, 0x5a, 0x400);
    for (blocks = 0; blocks < 69; ++blocks) {
        arg.block = blocks;
        memset(guarded, 0xa5, sizeof(guarded));
        assert(sbcall_cdvdopenconfig(&call) == 0);
        bytes = blocks * 15;
        assert(configReadBytes == bytes);
        /* Linux READCONFIG supplies only data, not the original block count. */
        arg.block = 0xdeadbeef;
        assert(sbcall_cdvdreadconfig(&call) == 0);
        for (i = 0; i < sizeof(guarded); ++i)
            assert(guarded[i] == (i >= 16 && i < 16 + bytes ? 0x5a : 0xa5));
        assert(sbcall_cdvdcloseconfig(&call) == 0 && configReadBytes == 0);
    }
    arg.block = 69;
    assert(sbcall_cdvdopenconfig(&call) < 0);
    arg.block = 3;
    assert(sbcall_cdvdopenconfig(&call) == 0);
    *(s32 *)sCmdCfgRecvBuff = 0;
    memset(guarded, 0xa5, sizeof(guarded));
    sbcall_cdvdreadconfig(&call);
    for (i = 0; i < sizeof(guarded); ++i) assert(guarded[i] == 0xa5);
    puts("CDVD config: 0..68 blocks, 45-byte Linux buffer guards, invalid count and failed read passed");
    return 0;
}
'''
generated = prefix + '\n'.join(function(name) for name in (
    'cdvdconfigStage2', 'cdvdreadconfigStage2', 'sbcall_cdvdopenconfig',
    'sbcall_cdvdcloseconfig', 'sbcall_cdvdreadconfig')) + main
if '--emit' in sys.argv:
    Path(sys.argv[sys.argv.index('--emit') + 1]).write_text(generated)
    sys.exit(0)
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'config.c'
    path.write_text(generated)
    binary = str(Path(temp) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', str(path), '-o', binary], check=True)
    subprocess.run([binary], check=True)
