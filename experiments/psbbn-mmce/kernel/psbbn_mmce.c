/* SPDX-License-Identifier: GPL-2.0-only
 * Linux 2.4.17 PS2 block driver for the MMCE/Neutrino ATAD RPC bridge.
 * Built in, replacing physical IDE at major 3 so PSBBN keeps /dev/hdaN.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/blk.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/ps2/sifdefs.h>
#include "block_rpc.h"

#define MMCE_MAJOR 3
#define MMCE_MINORS 64
#define RPC_TIMEOUT (10 * HZ)
static ps2sif_clientdata_t client __attribute__((aligned(64)));
static struct psbbn_block_packet tx __attribute__((aligned(64)));
static struct psbbn_block_packet rx __attribute__((aligned(64)));
static volatile int rpc_done __attribute__((aligned(64)));
static int rpc_dead;
static DECLARE_WAIT_QUEUE_HEAD(rpc_wait);
static unsigned int sequence, capacity, capabilities;
static struct hd_struct partitions[MMCE_MINORS];
static int sizes[MMCE_MINORS], blocksizes[MMCE_MINORS], sectorsizes[MMCE_MINORS];
static struct gendisk disk;
static spinlock_t pending_lock = SPIN_LOCK_UNLOCKED;
static struct buffer_head *pending_head, *pending_tail;
static DECLARE_MUTEX_LOCKED(pending_work);

static void rpc_callback(void *unused)
{
    (void)unused;
    rpc_done = 1;
    wake_up(&rpc_wait);
}

static int rpc_wait_done(void)
{
    long remaining = RPC_TIMEOUT;
    DECLARE_WAITQUEUE(wait, current);
    add_wait_queue(&rpc_wait, &wait);
    while (!rpc_done && remaining > 0) {
        set_current_state(TASK_UNINTERRUPTIBLE);
        if (!rpc_done) remaining = schedule_timeout(remaining);
    }
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&rpc_wait, &wait);
    if (!rpc_done) {
        /* A late DMA/callback may still arrive: retain and never reuse buffers. */
        rpc_dead = 1;
        printk(KERN_ERR "psbbn-mmce: RPC timeout; storage disabled\n");
        return -EIO;
    }
    return 0;
}

static int rpc_send(int function, unsigned int lba, unsigned int count, void *buffer)
{
    int rv, send_size = 64, receive_size = 64;
    unsigned int bytes = count * 512;
    unsigned long deadline = jiffies + RPC_TIMEOUT;
    if (rpc_dead) return -EIO;
    memset(&tx, 0, 64);
    tx.magic = PSBBN_BLOCK_MAGIC;
    tx.version = PSBBN_BLOCK_VERSION;
    tx.sequence = ++sequence;
    tx.lba = lba;
    tx.count = count;
    tx.bytes = bytes;
    if (function == PSBBN_BLOCK_READ) receive_size += bytes;
    if (function == PSBBN_BLOCK_WRITE) {
        memcpy(tx.data, buffer, bytes);
        send_size += bytes;
    }
    memset(&rx, 0, receive_size);
    rpc_done = 0;
    flush_cache_all();
    do {
        rv = ps2sif_callrpc(&client, function, SIF_RPCM_NOWAIT,
            &tx, send_size, &rx, receive_size, rpc_callback, NULL);
        if (rv != -SIF_RPCE_SENDP) break;
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(1);
    } while (time_before(jiffies, deadline));
    if (rv < 0) { rpc_dead = 1; return -EIO; }
    if (rpc_wait_done()) return -EIO;
    if (rx.magic != PSBBN_BLOCK_MAGIC || rx.version != PSBBN_BLOCK_VERSION ||
        rx.sequence != tx.sequence || rx.lba != lba || rx.count != count ||
        (capacity && rx.capacity != capacity)) { rpc_dead = 1; return -EIO; }
    if (rx.result) return rx.result < 0 ? rx.result : -EIO;
    if (rx.bytes != bytes) { rpc_dead = 1; return -EIO; }
    if (function == PSBBN_BLOCK_READ) memcpy(buffer, rx.data, bytes);
    return 0;
}

static int bridge_bind(void)
{
    unsigned long deadline = jiffies + RPC_TIMEOUT;
    int rv;
    memset(&client, 0, sizeof(client));
    do {
        rpc_done = 0;
        rv = ps2sif_bindrpc(&client, PSBBN_BLOCK_RPC_ID, SIF_RPCM_NOWAIT, rpc_callback, NULL);
        if (rv == 0) {
            if (rpc_wait_done()) return -EIO;
            if (client.serve) return 0;
        } else if (rv != -SIF_RPCE_SENDP) return -EIO;
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(1);
    } while (time_before(jiffies, deadline));
    rpc_dead = 1;
    return -ENODEV;
}

static int transfer_bh(struct buffer_head *bh)
{
    unsigned int done = 0, count, total = bh->b_size / 512;
    unsigned long lba = bh->b_rsector;
    int write = test_and_clear_bit(BH_Dirty, &bh->b_state), rv;
    if (MINOR(bh->b_rdev) != 0 || !total || bh->b_size % 512 ||
        lba >= capacity || total > capacity - lba) return -EINVAL;
    if (write && !(capabilities & PSBBN_BLOCK_WRITABLE)) return -EROFS;
    while (done < total) {
        count = total - done;
        if (count > PSBBN_BLOCK_MAX_SECTORS) count = PSBBN_BLOCK_MAX_SECTORS;
        rv = rpc_send(write ? PSBBN_BLOCK_WRITE : PSBBN_BLOCK_READ,
                      lba + done, count, bh->b_data + done * 512);
        if (rv) return rv;
        done += count;
    }
    return 0;
}

static int block_worker(void *unused)
{
    struct buffer_head *bh;
    unsigned long flags;
    int rv;
    (void)unused;
    daemonize();
    exit_files(current);
    strcpy(current->comm, "psbbn-mmce");
    spin_lock_irq(&current->sigmask_lock);
    sigfillset(&current->blocked);
    flush_signals(current);
    spin_unlock_irq(&current->sigmask_lock);
    current->flags |= PF_NOIO;
    for (;;) {
        down(&pending_work);
        spin_lock_irqsave(&pending_lock, flags);
        bh = pending_head;
        if (bh) {
            pending_head = bh->b_reqnext;
            if (!pending_head) pending_tail = NULL;
            bh->b_reqnext = NULL;
        }
        spin_unlock_irqrestore(&pending_lock, flags);
        if (!bh) continue;
        rv = transfer_bh(bh);
        if (rv) printk(KERN_ERR "psbbn-mmce: LBA=%lu bytes=%u failed=%d\n", bh->b_rsector, bh->b_size, rv);
        bh->b_end_io(bh, rv == 0);
    }
    return 0;
}

static int make_request(request_queue_t *q, int rw, struct buffer_head *bh)
{
    unsigned long flags;
    (void)q;
    if ((rw != READ && rw != READA && rw != WRITE) || rpc_dead) {
        buffer_IO_error(bh);
        return 0;
    }
    if (rw == WRITE) set_bit(BH_Dirty, &bh->b_state);
    else clear_bit(BH_Dirty, &bh->b_state);
    bh->b_reqnext = NULL;
    spin_lock_irqsave(&pending_lock, flags);
    if (pending_tail) pending_tail->b_reqnext = bh;
    else pending_head = bh;
    pending_tail = bh;
    spin_unlock_irqrestore(&pending_lock, flags);
    up(&pending_work);
    return 0;
}

static int block_open(struct inode *inode, struct file *file)
{
    unsigned int minor = MINOR(inode->i_rdev);
    if (minor >= MMCE_MINORS || !partitions[minor].nr_sects || rpc_dead) return -ENODEV;
    if ((file->f_mode & FMODE_WRITE) && !(capabilities & PSBBN_BLOCK_WRITABLE)) return -EROFS;
    return 0;
}

static int block_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned int minor = MINOR(inode->i_rdev);
    struct hd_geometry geometry;
    unsigned long sectors;
    (void)file;
    if (minor >= MMCE_MINORS) return -ENODEV;
    sectors = partitions[minor].nr_sects;
    switch (cmd) {
    case BLKGETSIZE: return put_user(sectors, (unsigned long *)arg);
    case BLKGETSIZE64: return put_user((u64)sectors * 512, (u64 *)arg);
    case HDIO_GETGEO:
        memset(&geometry, 0, sizeof(geometry));
        geometry.heads = 16; geometry.sectors = 63;
        geometry.cylinders = capacity / (16 * 63);
        geometry.start = partitions[minor].start_sect;
        return copy_to_user((void *)arg, &geometry, sizeof(geometry)) ? -EFAULT : 0;
    default: return blk_ioctl(inode->i_rdev, cmd, arg);
    }
}

static struct block_device_operations block_ops = {
    owner: THIS_MODULE,
    open: block_open,
    ioctl: block_ioctl,
};

static int __init psbbn_mmce_init(void)
{
    int i, rv;
    if (bridge_bind() || rpc_send(PSBBN_BLOCK_INFO, 0, 0, NULL)) {
        printk(KERN_ERR "psbbn-mmce: IOP block service unavailable\n");
        return -ENODEV;
    }
    capacity = rx.capacity;
    capabilities = rx.flags;
    if (!capacity || capacity > 0x0fffffffU) return -EINVAL;
    rv = register_blkdev(MMCE_MAJOR, "psbbn-mmce", &block_ops);
    if (rv) return rv;
    for (i = 0; i < MMCE_MINORS; ++i) {
        blocksizes[i] = 1024; sectorsizes[i] = 512;
        set_device_ro(MKDEV(MMCE_MAJOR, i), !(capabilities & PSBBN_BLOCK_WRITABLE));
    }
    blk_size[MMCE_MAJOR] = sizes;
    blksize_size[MMCE_MAJOR] = blocksizes;
    hardsect_size[MMCE_MAJOR] = sectorsizes;
    blk_init_queue(BLK_DEFAULT_QUEUE(MMCE_MAJOR), NULL);
    blk_queue_make_request(BLK_DEFAULT_QUEUE(MMCE_MAJOR), make_request);
    memset(&disk, 0, sizeof(disk));
    disk.major = MMCE_MAJOR; disk.major_name = "hd";
    disk.minor_shift = 6; disk.max_p = MMCE_MINORS;
    disk.part = partitions; disk.sizes = sizes; disk.nr_real = 1; disk.fops = &block_ops;
    partitions[0].nr_sects = capacity; partitions[0].nr_segs = 1;
    blk_dev[MMCE_MAJOR].gd = &disk;
    rv = kernel_thread(block_worker, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
    if (rv < 0) {
        blk_dev[MMCE_MAJOR].gd = NULL;
        blk_cleanup_queue(BLK_DEFAULT_QUEUE(MMCE_MAJOR));
        blk_size[MMCE_MAJOR] = NULL; blksize_size[MMCE_MAJOR] = NULL; hardsect_size[MMCE_MAJOR] = NULL;
        unregister_blkdev(MMCE_MAJOR, "psbbn-mmce");
        return rv;
    }
    add_gendisk(&disk);
    printk(KERN_INFO "psbbn-mmce: %u sectors at /dev/hda, %s\n", capacity,
        capabilities & PSBBN_BLOCK_WRITABLE ? "read/write" : "read-only");
    register_disk(&disk, MKDEV(MMCE_MAJOR, 0), MMCE_MINORS, &block_ops, capacity);
    return 0;
}
module_init(psbbn_mmce_init);
MODULE_LICENSE("GPL");
