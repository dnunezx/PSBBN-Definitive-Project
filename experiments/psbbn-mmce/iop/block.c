/* SPDX-License-Identifier: GPL-2.0-only OR AFL-3.0
 * Linux block RPC over Neutrino ATAD. No physical drive initialization here.
 */
#ifndef PSBBN_BLOCK_HOST_TEST
#include <irx.h>
#include <loadcore.h>
#include <atad.h>
#include <sifrpc.h>
#include <sysclib.h>
#include <thbase.h>
#endif
#include "block_rpc.h"
#ifndef PSBBN_BLOCK_HOST_TEST
IRX_ID("psbbn_block_rpc", 0, 6);
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static struct psbbn_block_packet request __attribute__((aligned(64)));
#endif
static struct psbbn_block_packet reply __attribute__((aligned(64)));
static int writable;

static void *handle(int function, void *data, int size)
{
    struct psbbn_block_packet *req = data;
    ata_devinfo_t *info;
    unsigned int bytes;
    memset(&reply, 0, sizeof(reply));
    reply.magic = PSBBN_BLOCK_MAGIC;
    reply.version = PSBBN_BLOCK_VERSION;
    reply.result = -22;
    info = sceAtaInit(0);
    if (!info || !info->exists || info->has_packet || !info->total_sectors) { reply.result = -19; return &reply; }
    reply.capacity = info->total_sectors;
#ifdef PSBBN_BLOCK_CAPACITY
    /* Emulator HDD padding stays outside the disk visible to Linux. */
    if (reply.capacity > PSBBN_BLOCK_CAPACITY) reply.capacity = PSBBN_BLOCK_CAPACITY;
#endif
    reply.flags = writable ? PSBBN_BLOCK_WRITABLE : 0;
    if (size < (int)PSBBN_BLOCK_HEADER_SIZE) return &reply;
    reply.sequence = req->sequence;
    reply.lba = req->lba;
    reply.count = req->count;
    if (req->magic != PSBBN_BLOCK_MAGIC || req->version != PSBBN_BLOCK_VERSION) return &reply;
    if (function == PSBBN_BLOCK_INFO) {
        if (size == 64 && !req->count && !req->bytes) reply.result = 0;
        return &reply;
    }
    /* ATAD's flush is a stub; do not promise durable flush semantics. */
    if (function == PSBBN_BLOCK_FLUSH) { reply.result = -95; return &reply; }
    if (!req->count || req->count > PSBBN_BLOCK_MAX_SECTORS || req->lba >= reply.capacity ||
        req->count > reply.capacity - req->lba) return &reply;
    bytes = req->count * 512;
    if (req->bytes != bytes) return &reply;
    if (function == PSBBN_BLOCK_READ && size == 64) {
        reply.result = sceAtaDmaTransfer(0, reply.data, req->lba, req->count, ATA_DIR_READ);
        if (!reply.result) reply.bytes = bytes;
        else memset(reply.data, 0, bytes);
    } else if (function == PSBBN_BLOCK_WRITE && size == (int)(64 + bytes)) {
        if (!writable) reply.result = -30;
        else reply.result = sceAtaDmaTransfer(0, req->data, req->lba, req->count, ATA_DIR_WRITE);
        if (!reply.result) reply.bytes = bytes;
    }
    return &reply;
}

#ifndef PSBBN_BLOCK_HOST_TEST
static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, PSBBN_BLOCK_RPC_ID, handle, &request, NULL, NULL, &queue);
    sceSifRpcLoop(&queue);
}

int _start(int argc, char **argv)
{
    iop_thread_t thread;
    int id, i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write")) writable = 1;
        else return MODULE_NO_RESIDENT_END;
    }
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
#endif
