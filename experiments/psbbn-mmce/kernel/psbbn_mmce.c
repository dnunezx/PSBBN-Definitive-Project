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
#include <linux/proc_fs.h>
#include <linux/kernel_stat.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/ps2/sifdefs.h>
#include "block_rpc.h"

#define MMCE_MAJOR 3
#define MMCE_MINORS 64
#define RPC_TIMEOUT (10 * HZ)
/* Keep the queued direction separate from BH_Dirty and JBD's private bits. */
#define BH_MMCE_WRITE (BH_PrivateStart + 16)
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
static DECLARE_WAIT_QUEUE_HEAD(pending_work);
static unsigned long read_requests, write_requests, read_sectors, write_sectors;
static unsigned long rpc_retries, rpc_errors, rpc_us, rpc_max_us, flush_us;
static unsigned long range_flushes, full_flushes, request_bins[7];
static unsigned int card_stats[7];

static unsigned long clock_us(void)
{
    struct timeval tv;
    do_gettimeofday(&tv);
    return (unsigned long)tv.tv_sec * 1000000UL + tv.tv_usec;
}

static int io_report(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    int size;
    (void)start; (void)data;
    if (off) return 0;
    size = sprintf(page, "psbbn_mmce_io_v1\nread_requests=%lu write_requests=%lu read_sectors=%lu write_sectors=%lu\n"
        "rpc_us=%lu rpc_max_us=%lu retries=%lu errors=%lu flush_us=%lu range_flushes=%lu full_flushes=%lu\n"
        "sizes_sectors_1_2_4_8_16_32_64=%lu,%lu,%lu,%lu,%lu,%lu,%lu\n"
        "card_stats_available=%u card_read_us=%u card_max_read_us=%u sharing_wait_us=%u combined_reads=%u legacy_reads=%u cache_hits=%u card_errors=%u\n"
        "swap_in_pages=%u swap_out_pages=%u\n",
        read_requests, write_requests, read_sectors, write_sectors, rpc_us, rpc_max_us, rpc_retries,
        rpc_errors, flush_us, range_flushes, full_flushes,
        request_bins[0], request_bins[1], request_bins[2], request_bins[3], request_bins[4], request_bins[5], request_bins[6],
        (capabilities & PSBBN_BLOCK_IO_STATS) != 0, card_stats[0], card_stats[1], card_stats[2],
        card_stats[3], card_stats[4], card_stats[5], card_stats[6], kstat.pswpin, kstat.pswpout);
    *eof = 1;
    return size < count ? size : count;
}

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

static void copy_payload(struct buffer_head *bh, unsigned int offset,
                         unsigned int bytes, unsigned char *payload, int write)
{
    while (bytes) {
        unsigned int length = bh->b_size - offset;
        if (length > bytes) length = bytes;
        if (write) memcpy(payload, bh->b_data + offset, length);
        else memcpy(bh->b_data + offset, payload, length);
        payload += length;
        bytes -= length;
        bh = bh->b_reqnext;
        offset = 0;
    }
}

static int rpc_send(int function, unsigned int lba, unsigned int count,
                    struct buffer_head *bh, unsigned int offset)
{
    int rv, send_size = 64, receive_size = 64;
    unsigned int bytes = count * 512;
    unsigned long deadline = jiffies + RPC_TIMEOUT;
    unsigned long began, flushed, elapsed;
    unsigned int bin;
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
        copy_payload(bh, offset, bytes, tx.data, 1);
        send_size += bytes;
    }
    memset(&rx, 0, receive_size);
    rpc_done = 0;
    began = clock_us();
    /* These two DMA buffers are cache-line aligned and exclusively owned until
     * callback. Keep whole-cache maintenance for transfers >= R5900's 8 KiB
     * D-cache; smaller requests need only writeback/invalidate of their ranges. */
    if (send_size >= 8192 || receive_size >= 8192) { flush_cache_all(); ++full_flushes; }
    else {
        dma_cache_wback_inv((unsigned long)&tx, send_size);
        dma_cache_wback_inv((unsigned long)&rx, receive_size);
        ++range_flushes;
    }
    flushed = clock_us();
    flush_us += flushed - began;
    if (function == PSBBN_BLOCK_READ || function == PSBBN_BLOCK_WRITE) {
        for (bin = 0; bin < 6 && count > (1U << bin); ++bin) {}
        ++request_bins[bin];
        if (function == PSBBN_BLOCK_READ) { ++read_requests; read_sectors += count; }
        else { ++write_requests; write_sectors += count; }
    }
    do {
        rv = ps2sif_callrpc(&client, function, SIF_RPCM_NOWAIT,
            &tx, send_size, &rx, receive_size, rpc_callback, NULL);
        if (rv != -SIF_RPCE_SENDP) break;
        ++rpc_retries;
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(1);
    } while (time_before(jiffies, deadline));
    if (rv < 0) { rpc_dead = 1; ++rpc_errors; return -EIO; }
    if (rpc_wait_done()) { ++rpc_errors; return -EIO; }
    elapsed = clock_us() - flushed;
    rpc_us += elapsed;
    if (elapsed > rpc_max_us) rpc_max_us = elapsed;
    if (rx.magic != PSBBN_BLOCK_MAGIC || rx.version != PSBBN_BLOCK_VERSION ||
        rx.sequence != tx.sequence || rx.lba != lba || rx.count != count ||
        (capacity && rx.capacity != capacity)) { rpc_dead = 1; return -EIO; }
    if (rx.flags & PSBBN_BLOCK_IO_STATS) memcpy(card_stats, rx.reserved, sizeof(card_stats));
    if (rx.result) { ++rpc_errors; return rx.result < 0 ? rx.result : -EIO; }
    if (rx.bytes != bytes) { rpc_dead = 1; return -EIO; }
    if (function == PSBBN_BLOCK_READ) copy_payload(bh, offset, bytes, rx.data, 0);
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

static unsigned int buffer_sectors(const struct buffer_head *bh)
{
    unsigned int count = bh->b_size / 512;
    unsigned long lba = bh->b_rsector;

    if (MINOR(bh->b_rdev) != 0 || bh->b_size % 512 ||
        lba >= capacity || count > capacity - lba) return 0;
    return count;
}

static struct buffer_head *take_batch(unsigned int *sectors, int *write)
{
    struct buffer_head *first, *last, *next;
    unsigned int count;
    unsigned long flags;

    wait_event(pending_work, pending_head != NULL);
    spin_lock_irqsave(&pending_lock, flags);
    first = last = pending_head;
    *sectors = buffer_sectors(first);
    *write = test_bit(BH_MMCE_WRITE, &first->b_state);

    /* Keep FIFO order and take only adjacent buffers already in the queue. */
    while (*sectors && *sectors < PSBBN_BLOCK_MAX_SECTORS &&
           (next = last->b_reqnext) != NULL &&
           next->b_rdev == first->b_rdev &&
           next->b_rsector == first->b_rsector + *sectors &&
           test_bit(BH_MMCE_WRITE, &next->b_state) == *write) {
        count = buffer_sectors(next);
        if (!count || count > PSBBN_BLOCK_MAX_SECTORS - *sectors) break;
        *sectors += count;
        last = next;
    }
    pending_head = last->b_reqnext;
    if (!pending_head) pending_tail = NULL;
    last->b_reqnext = NULL;
    spin_unlock_irqrestore(&pending_lock, flags);
    return first;
}

static int transfer_batch(struct buffer_head *bh, unsigned int total, int write)
{
    unsigned int done = 0, count;
    int rv;

    if (!total) return -EINVAL;
    if (write && !(capabilities & PSBBN_BLOCK_WRITABLE)) return -EROFS;
    /* Only a single oversized buffer can require more than one RPC. */
    while (done < total) {
        count = total - done;
        if (count > PSBBN_BLOCK_MAX_SECTORS) count = PSBBN_BLOCK_MAX_SECTORS;
        rv = rpc_send(write ? PSBBN_BLOCK_WRITE : PSBBN_BLOCK_READ,
                      bh->b_rsector + done, count, bh, done * 512);
        if (rv) return rv;
        done += count;
    }
    return 0;
}

static int block_worker(void *unused)
{
    struct buffer_head *bh, *next;
    unsigned int sectors;
    int rv, write;
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
        bh = take_batch(&sectors, &write);
        rv = transfer_batch(bh, sectors, write);
        if (rv) printk(KERN_ERR "psbbn-mmce: LBA=%lu sectors=%u failed=%d\n", bh->b_rsector, sectors, rv);
        while (bh) {
            next = bh->b_reqnext;
            bh->b_reqnext = NULL;
            clear_bit(BH_MMCE_WRITE, &bh->b_state);
            bh->b_end_io(bh, rv == 0);
            bh = next;
        }
    }
    return 0;
}

static int make_request(request_queue_t *q, int rw, struct buffer_head *bh)
{
    unsigned long flags;
    int wake;
    (void)q;
    if ((rw != READ && rw != READA && rw != WRITE) || rpc_dead) {
        buffer_IO_error(bh);
        return 0;
    }
    if (rw == WRITE) set_bit(BH_MMCE_WRITE, &bh->b_state);
    else clear_bit(BH_MMCE_WRITE, &bh->b_state);
    bh->b_reqnext = NULL;
    spin_lock_irqsave(&pending_lock, flags);
    wake = !pending_head;
    if (pending_tail) pending_tail->b_reqnext = bh;
    else pending_head = bh;
    pending_tail = bh;
    spin_unlock_irqrestore(&pending_lock, flags);
    if (wake) wake_up(&pending_work);
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
    if (bridge_bind() || rpc_send(PSBBN_BLOCK_INFO, 0, 0, NULL, 0)) {
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
    create_proc_read_entry("psbbn_mmce", 0444, NULL, io_report, NULL);
    printk(KERN_INFO "psbbn-mmce: %u sectors at /dev/hda, %s\n", capacity,
        capabilities & PSBBN_BLOCK_WRITABLE ? "read/write" : "read-only");
    register_disk(&disk, MKDEV(MMCE_MAJOR, 0), MMCE_MINORS, &block_ops, capacity);
    return 0;
}
module_init(psbbn_mmce_init);
MODULE_LICENSE("GPL");
