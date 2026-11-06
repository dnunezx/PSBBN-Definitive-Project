/* Read-only consumers of the existing FHI and ATAD libraries. */
#include <irx.h>
#include <loadcore.h>
#include <atad.h>
#include <sifrpc.h>
#include <sysclib.h>
#include <thbase.h>
#include "fhi.h"
#include "suite_rpc.h"
IRX_ID("psbbn_suite_rpc", 0, 4);
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static struct suite_reply reply __attribute__((aligned(64)));
static unsigned char request[64] __attribute__((aligned(64)));

static void *handle(int function, void *data, int size)
{
    struct suite_request *req = data;
    ata_devinfo_t *info;
    memset(&reply, 0, 64);
    memset(reply.data, 0xcc, sizeof(reply.data));
    reply.result = -22;
    reply.fhi_sectors = fhi_size(FHI_FID_ATA0);
    info = sceAtaInit(0);
    if (info) {
        reply.exists = info->exists;
        reply.packet = info->has_packet;
        reply.sectors = info->total_sectors;
        reply.security = info->security_status;
    }
    if (function == 3) { reply.result = 0; return &reply; }
    if (function == 4) { reply.result = fhi_close(FHI_FID_ATA0); return &reply; }
    if (size < (int)sizeof(*req)) return &reply;
    reply.lba = req->lba;
    reply.count = req->count;
    /* Reject invalid ranges before calling either upstream driver. */
    if (!suite_range_valid(reply.fhi_sectors, req->lba, req->count))
        return &reply;
    if (function == 1)
        reply.result = fhi_read(FHI_FID_ATA0, reply.data, req->lba, req->count);
    else if (function == 2 && reply.exists && !reply.packet)
        reply.result = sceAtaDmaTransfer(0, reply.data, req->lba, req->count, ATA_DIR_READ);
    return &reply;
}

static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, SUITE_RPC_ID, handle, request, NULL, NULL, &queue);
    sceSifRpcLoop(&queue);
}

int _start(int argc, char **argv)
{
    iop_thread_t thread;
    int id;
    (void)argc; (void)argv;
    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C;
    thread.thread = rpc_thread;
    thread.priority = 40;
    thread.stacksize = 0x1000;
    id = CreateThread(&thread);
    if (id < 0) return MODULE_NO_RESIDENT_END;
    if (StartThread(id, NULL) < 0) { DeleteThread(id); return MODULE_NO_RESIDENT_END; }
    return MODULE_RESIDENT_END;
}
