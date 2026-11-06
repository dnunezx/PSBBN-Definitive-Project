#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define PSBBN_FHI_HOST_TEST
typedef int64_t s64;
typedef uint32_t u32;
typedef struct { int attr, option, initial, maximum; } iop_sema_t;
static int depth, seek_fail, close_fail, reads, writes;
static s64 last_offset;
static int CreateSema(iop_sema_t *s) { (void)s; return 1; }
static void DeleteSema(int id) { assert(id == 1); }
static void WaitSema(int id) { assert(id == 1 && depth == 0); ++depth; }
static void SignalSema(int id) { assert(id == 1 && depth == 1); --depth; }
#include "../vendor/mmcefhi/src/mmce_fhi.c"
s64 mmcedrv_lseek64(int fd, s64 offset, int whence)
{ assert(fd == 9 && !whence && depth == 1); last_offset = offset; return seek_fail ? -1 : offset; }
int mmcedrv_read_sector(int fd, u32 sector, u32 count, void *buffer)
{ (void)fd; (void)sector; (void)buffer; return count; }
int mmcedrv_read(int fd, int size, void *buffer)
{ assert(fd == 9 && depth == 1); (void)buffer; ++reads; return size; }
int mmcedrv_write(int fd, int size, const void *buffer)
{ assert(fd == 9 && depth == 1); (void)buffer; ++writes; return size; }
int mmcedrv_close_handle(int fd)
{ assert(fd == 9 && depth == 1); return close_fail ? -1 : 0; }
int main(void)
{
    unsigned char data[512];
    assert(!mmce_fhi_init());
    fhi.file[FHI_FID_ATA0].id = 9;
    fhi.file[FHI_FID_ATA0].size = (uint64_t)7 * 1024 * 1024 * 1024;
    assert(fhi_read(FHI_FID_ATA0, data, 8388608, 1) == 1);
    assert(last_offset == (s64)4 * 1024 * 1024 * 1024);
    assert(fhi_write(FHI_FID_ATA0, data, 8388608, 1) == 1);
    seek_fail = 1;
    assert(!fhi_read(FHI_FID_ATA0, data, 0, 1));
    assert(!fhi_write(FHI_FID_ATA0, data, 0, 1));
    assert(reads == 1 && writes == 1 && depth == 0);
    close_fail = 1;
    assert(fhi_close(FHI_FID_ATA0) == -1 && fhi_size(FHI_FID_ATA0) == 14680064);
    close_fail = 0;
    assert(!fhi_close(FHI_FID_ATA0) && !fhi_size(FHI_FID_ATA0));
    assert(fhi.file[FHI_FID_ATA0].id == -1 && depth == 0);
    mmce_fhi_deinit();
    puts("FHI lifecycle: 64-bit seeks, failed seek blocks reads/writes, failed close retains handle, successful close releases it");
    return 0;
}
