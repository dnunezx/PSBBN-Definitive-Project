/* A consumer of the existing ATAD library, not an ATA implementation.
 * Fixed request: device 0, LBA 0, one sector, READ. No write opcode/API.
 */
#include <irx.h>
#include <loadcore.h>
#include <atad.h>
#include <sifrpc.h>
#include <sysclib.h>
#include <thbase.h>
#include "probe_rpc.h"
IRX_ID("psbbn_read_rpc", 0, 2);
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static struct probe_reply reply __attribute__((aligned(64)));
static unsigned char request[64] __attribute__((aligned(64)));

static void *read_sector(int function, void *data, int size)
{
    ata_devinfo_t *info;
    (void)data;
    (void)size;
    memset(&reply, 0, sizeof(reply));
    reply.result = -1;
    if (function != 1) return &reply;
    info = sceAtaInit(0);
    if (!info) return &reply;
    reply.exists = info->exists;
    reply.has_packet = info->has_packet;
    reply.total_sectors = info->total_sectors;
    reply.security_status = info->security_status;
    if (info->exists && !info->has_packet && info->total_sectors)
        reply.result = sceAtaDmaTransfer(0, reply.sector, 0, 1, ATA_DIR_READ);
    return &reply;
}

static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, PSBBN_PROBE_RPC, read_sector, request, NULL, NULL, &queue);
    sceSifRpcLoop(&queue);
}

int _start(int argc, char **argv)
{
    iop_thread_t thread;
    int id;
    (void)argc;
    (void)argv;
    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C;
    thread.thread = rpc_thread;
    thread.priority = 40;
    thread.stacksize = 0x1000;
    id = CreateThread(&thread);
    if (id < 0) return MODULE_NO_RESIDENT_END;
    if (StartThread(id, NULL) < 0) {
        DeleteThread(id);
        return MODULE_NO_RESIDENT_END;
    }
    return MODULE_RESIDENT_END;
}
