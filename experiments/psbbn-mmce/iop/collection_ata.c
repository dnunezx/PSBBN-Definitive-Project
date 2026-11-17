/* SPDX-License-Identifier: MIT
 * APA/PFS staging backend. Only this temporary backend accesses the COW log;
 * Linux still receives the original, unchanged MMCE/Neutrino driver stack.
 */
#include <irx.h>
#include <loadcore.h>
#include <iomanX.h>
#include <sifrpc.h>
#include <thbase.h>
#include <thsemap.h>
#include <sysclib.h>
#include <stdio.h>
#include <atad.h>
#include "suite_rpc.h"
#include "collection_txn.h"
#include "collection_io.h"
IRX_ID("psbbn_collection_ata", 1, 0);
extern struct irx_export_table _exp_atad;
static struct ct_state transaction __attribute__((aligned(64)));
static struct collection_io_diag io_diag;
static int descriptors[2] = {-1, -1}, lock = -1;
static char paths[2][96];
static ata_devinfo_t devices[2];
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static struct suite_request input __attribute__((aligned(64)));
static struct suite_reply output __attribute__((aligned(64)));
static int io_reopen(void *c, int which);
static int io_length(void *c, int which, uint64_t *bytes);
static int exact(int which, uint64_t at, void *data, unsigned int bytes, int write)
{
    unsigned int used = 0;
    int n;
    s64 position = descriptors[which] < 0 ? -1 : iomanX_lseek64(descriptors[which], at, SEEK_SET);
    if (position != (s64)at) {
        uint64_t length;
        collection_io_failure(&io_diag, COLLECTION_IO_SEEK, which, at, bytes, position);
        /* Console traces show this fault both before the first record and
         * after four staged records. The base image remains unchanged during
         * active COW staging; reacquiring its READ handle preserves the staged
         * overlay and journal. Never retry a journal operation, image write,
         * prepared commit/recovery, or a second failed image seek.
         * A failed close/reopen/size check/second seek is still a hard error. */
        if (which || write || !transaction.active || io_diag.recovery_attempted) return -1;
        io_diag.recovery_attempted = 1;
        if (io_reopen(NULL, 0) || io_length(NULL, 0, &length) || length != transaction.bytes ||
            iomanX_lseek64(descriptors[0], at, SEEK_SET) != (s64)at) return -1;
        io_diag.recovered = 1;
    }
    while (used < bytes) {
        n = write ? iomanX_write(descriptors[which], (unsigned char *)data + used, bytes - used) :
                    iomanX_read(descriptors[which], (unsigned char *)data + used, bytes - used);
        if (n <= 0 || (unsigned int)n > bytes - used) {
            collection_io_failure(&io_diag, write ? COLLECTION_IO_WRITE : COLLECTION_IO_READ,
                which, at + used, bytes - used, n);
            return -1;
        }
        used += n;
    }
    return 0;
}
static int io_read(void *c, int which, uint64_t at, void *data, unsigned int bytes)
{ (void)c; return exact(which, at, data, bytes, 0); }
static int io_write(void *c, int which, uint64_t at, const void *data, unsigned int bytes)
{ (void)c; return exact(which, at, (void *)data, bytes, 1); }
static int io_length(void *c, int which, uint64_t *bytes)
{
    s64 size;
    (void)c;
    size = iomanX_lseek64(descriptors[which], 0, SEEK_END);
    if (size < 0) {
        collection_io_failure(&io_diag, COLLECTION_IO_LENGTH, which, 0, 0, size);
        return -1;
    }
    *bytes = size; return 0;
}
static int io_reopen(void *c, int which)
{
    int fd;
    (void)c;
    fd = descriptors[which]; descriptors[which] = -1;
    if (fd >= 0) {
        int rv = iomanX_close(fd);
        if (rv < 0) { collection_io_failure(&io_diag, COLLECTION_IO_CLOSE, which, 0, 0, rv); return -1; }
    }
    descriptors[which] = iomanX_open(paths[which], FIO_O_RDWR, 0);
    if (descriptors[which] < 0)
        collection_io_failure(&io_diag, COLLECTION_IO_OPEN, which, 0, 0, descriptors[which]);
    return descriptors[which] < 0 ? -1 : 0;
}
static int io_resize(void *c, int which, uint64_t bytes)
{
    int fd;
    (void)c;
    if (which != 1 || bytes) return -1;
    fd = descriptors[1]; descriptors[1] = -1;
    if (fd >= 0) {
        int rv = iomanX_close(fd);
        if (rv < 0) { collection_io_failure(&io_diag, COLLECTION_IO_CLOSE, which, 0, 0, rv); return -1; }
    }
    descriptors[1] = iomanX_open(paths[1], FIO_O_RDWR | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (descriptors[1] < 0)
        collection_io_failure(&io_diag, COLLECTION_IO_OPEN, which, 0, 0, descriptors[1]);
    return descriptors[1] < 0 ? -1 : 0;
}
ata_devinfo_t *ata_get_devinfo(int device)
{ return device < 0 || device > 1 ? NULL : &devices[device]; }
int ata_device_sector_io(int device, void *data, u32 lba, u32 count, int direction)
{
    int rv;
    if (device || !devices[0].exists || (direction != ATA_DIR_READ && direction != ATA_DIR_WRITE)) return -5;
    WaitSema(lock);
    rv = ct_sectors(&transaction, lba, count, data, direction == ATA_DIR_WRITE);
    SignalSema(lock); return rv ? -5 : 0;
}
int ata_reset_devices(void) { return 0; }
int ata_io_start(void *buf, u32 blkcount, u16 feature, u16 nsector, u16 sector, u16 lcyl, u16 hcyl, u16 select, u16 command)
{ (void)buf;(void)blkcount;(void)feature;(void)nsector;(void)sector;(void)lcyl;(void)hcyl;(void)select;(void)command;return -5; }
int ata_io_finish(void) { return -5; }
int ata_get_error(void) { return transaction.failed ? -5 : 0; }
int ata_device_sce_sec_set_password(int d, void *p) { (void)d;(void)p;return -5; }
int ata_device_sce_sec_unlock(int d, void *p) { (void)d;(void)p;return 0; }
int ata_device_sce_sec_erase(int d) { (void)d;return -5; }
int ata_device_idle(int d, int p) { (void)d;(void)p;return 0; }
int ata_device_sce_identify_drive(int d, void *p) { (void)d;memset(p, 0, 512);return 0; }
int ata_device_smart_get_status(int d) { (void)d;return 0; }
int ata_device_smart_save_attr(int d) { (void)d;return 0; }
int ata_device_flush_cache(int d) { (void)d;return transaction.failed ? -5 : 0; }
int ata_device_idle_immediate(int d) { (void)d;return 0; }
int _exit(void) { return 0; }
static void *control(int function, void *data, int size)
{
    struct suite_request *req = data;
    uint64_t bytes;
    int rv = -1, i;
    memset(&output, 0, sizeof(output)); output.result = -1;
    if (size != sizeof(*req)) return &output;
    WaitSema(lock);
    if (function == 10 && req->lba < 2 && descriptors[0] < 0) {
        memset(&io_diag, 0, sizeof(io_diag));
        sprintf(paths[0], "mmce%u:/PSBBN/psbbn.img", (unsigned int)req->lba);
        sprintf(paths[1], "mmce%u:/PSBBN/collection-txn.bin", (unsigned int)req->lba);
        descriptors[0] = iomanX_open(paths[0], FIO_O_RDWR, 0);
        descriptors[1] = iomanX_open(paths[1], FIO_O_RDWR | FIO_O_CREAT, 0666);
        for (i = 0; i < 2; ++i) if (descriptors[i] < 0)
            collection_io_failure(&io_diag, COLLECTION_IO_OPEN, i, 0, 0, descriptors[i]);
        memset(&transaction, 0, sizeof(transaction)); transaction.identity = req->count;
        transaction.bytes = 7516192768ULL;
        transaction.io.read = io_read; transaction.io.write = io_write; transaction.io.length = io_length;
        transaction.io.resize = io_resize; transaction.io.reopen = io_reopen;
        if (descriptors[0] >= 0 && descriptors[1] >= 0 && !io_length(NULL, 0, &bytes) && bytes == transaction.bytes) {
            rv = ct_begin(&transaction);
            if (!rv) { devices[0].exists = 1; devices[0].total_sectors = transaction.bytes / 512; }
        }
    } else if (function == 11) rv = ct_commit(&transaction);
    else if (function == 12) {
        rv = 0;
        for (i = 0; i < 2; ++i) {
            if (descriptors[i] >= 0 && iomanX_close(descriptors[i]) < 0) rv = -1;
            descriptors[i] = -1;
        }
        devices[0].exists = 0;
    } else if (function == 13) rv = transaction.failed ? -1 : 0;
    else if (function == 14) {
        io_diag.records = transaction.records; io_diag.pages = transaction.pages; io_diag.failed = transaction.failed;
        memcpy(output.data, &io_diag, sizeof(io_diag)); rv = 0;
    }
    output.result = rv; output.count = transaction.records; output.lba = transaction.pages;
    SignalSema(lock); return &output;
}
static void rpc_thread(void *p)
{
    (void)p; sceSifInitRpc(0); sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, SUITE_RPC_ID, control, &input, NULL, NULL, &queue); sceSifRpcLoop(&queue);
}
int _start(int argc, char **argv)
{
    iop_sema_t semaphore = {0, 0, 1, 1};
    iop_thread_t thread;
    int id;
    (void)argc;(void)argv;
    lock = CreateSema(&semaphore);
    if (lock < 0 || RegisterLibraryEntries(&_exp_atad)) return MODULE_NO_RESIDENT_END;
    memset(&thread, 0, sizeof(thread)); thread.attr = TH_C; thread.thread = rpc_thread;
    thread.priority = 40; thread.stacksize = 0x4000;
    id = CreateThread(&thread);
    if (id < 0 || StartThread(id, NULL) < 0) return MODULE_NO_RESIDENT_END;
    return MODULE_RESIDENT_END;
}
