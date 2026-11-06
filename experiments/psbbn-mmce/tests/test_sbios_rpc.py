"""Compile the production RPC completion handler against small host mocks."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / 'vendor/tge/sbios/sifrpc.c').read_text()
match = re.search(r'^static void _request_end\([^\n]*\)\n\{', source, re.M)
assert match
end = source.index('{', match.start()) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[match.start():end]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint32_t u32;
typedef void (*SifRpcEndFunc_t)(void *);
typedef struct {
    struct { void *pkt_addr; int sema_id; } hdr;
    SifRpcEndFunc_t end_function;
    void *end_param, *server, *buff, *cbuff;
} SifRpcClientData_t;
typedef struct { SifRpcClientData_t *client; void *pkt_addr; u32 cid; void *server, *buff, *cbuff; } SifRpcRendPkt_t;
static volatile u32 stale_rpc_completions;
static int frees, callbacks;
static void *last_free;
static void core_save_disable(u32 *status) { *status = 1; }
static void core_restore(u32 status) { assert(status == 1); }
static void rpc_packet_free(void *p) { assert(p != NULL); ++frees; last_free = p; }
static void done(void *p) { assert(p == &callbacks); ++callbacks; }
'''
main = r'''
int main(void)
{
    int first, second;
    SifRpcClientData_t client = {{&first, 0}, done, &callbacks, NULL, NULL, NULL};
    SifRpcRendPkt_t request = {&client, &first, 0x8000000a, NULL, NULL, NULL};
    _request_end(&request, NULL);
    assert(frees == 1 && last_free == &first && callbacks == 1 && client.hdr.sema_id == 1 && !client.hdr.pkt_addr);
    _request_end(&request, NULL);
    assert(frees == 1 && callbacks == 1 && client.hdr.sema_id == 1 && stale_rpc_completions == 1);
    client.hdr.pkt_addr = &second;
    /* ROM/legacy IOP servers can omit the echoed packet address. */
    request.pkt_addr = NULL;
    request.cid = 0x80000009;
    request.server = &first; request.buff = &second;
    _request_end(&request, NULL);
    assert(frees == 2 && last_free == &second && callbacks == 2 && client.server == &first && client.buff == &second);
    request.client = NULL;
    _request_end(&request, NULL);
    assert(frees == 2 && callbacks == 2);
    puts("SBIOS RPC: normal completion, duplicate, legacy bind and null client passed");
    return 0;
}
'''
Path(sys.argv[1]).write_text(prefix + function + main)
