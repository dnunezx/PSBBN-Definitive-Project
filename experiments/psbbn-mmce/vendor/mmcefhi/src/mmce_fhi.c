#ifndef PSBBN_FHI_HOST_TEST
#include <thsemap.h>
#include <thbase.h>
#endif

#include "module_debug.h"
#include "mmcedrv_config.h"

#include "fhi_fileid.h"
#include "mmce_io_stats.h"
#ifndef PSBBN_FHI_HOST_TEST
#include <sysclib.h>
#endif

/* Small exact-range cache, not speculative read-ahead. All users share this
 * semaphore; a write invalidates overlapping ranges before attempting I/O. */
#define ATA_CACHE_LINES 4
#define ATA_CACHE_BYTES 4096
static unsigned char ata_cache[ATA_CACHE_LINES][ATA_CACHE_BYTES] __attribute__((aligned(64)));
static unsigned int cache_lba[ATA_CACHE_LINES], cache_sectors[ATA_CACHE_LINES], cache_next;
static int combined_disabled;
static struct mmce_io_stats io_stats;
extern unsigned int mmce_sio2_sharing_us;
extern unsigned int mmce_io_clock(void);

static unsigned int clock_us(unsigned int start)
{
    unsigned int ticks = mmce_io_clock() - start;
    return ticks / 36864 * 1000 + ticks % 36864 * 1000 / 36864;
}
static void total(unsigned int *value, unsigned int add)
{ *value = ~0U - *value < add ? ~0U : *value + add; }
static void invalidate(unsigned int lba, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < ATA_CACHE_LINES; ++i)
        if (cache_sectors[i] && (unsigned long long)lba < (unsigned long long)cache_lba[i] + cache_sectors[i] &&
            (unsigned long long)cache_lba[i] < (unsigned long long)lba + count) cache_sectors[i] = 0;
}

struct fhi_fileid fhi = {.magic = MODULE_SETTINGS_MAGIC};

/* Local fix: the pinned upstream branch never creates this semaphore. */
static int mmce_io_sema = -1;

int mmce_fhi_init(void)
{
    iop_sema_t sema = {0, 0, 1, 1};
    mmce_io_sema = CreateSema(&sema);
    memset(cache_sectors, 0, sizeof(cache_sectors));
    memset(&io_stats, 0, sizeof(io_stats));
    cache_next = 0; combined_disabled = 0;
    return mmce_io_sema < 0 ? -1 : 0;
}

void mmce_fhi_deinit(void)
{
    if (mmce_io_sema >= 0) DeleteSema(mmce_io_sema);
    mmce_io_sema = -1;
}

extern s64 mmcedrv_lseek64(int fd, s64 offset, int whence);
extern int mmcedrv_read_sector(int fd, u32 sector, u32 count, void *buffer);
extern int mmcedrv_read(int fd, int size, void *ptr);
extern int mmcedrv_write(int fd, int size, const void *ptr);
extern int mmcedrv_close_handle(int fd);

int fhi_close(int file_handle)
{
    int result;
    if (file_handle != FHI_FID_ATA0) return -22;
    WaitSema(mmce_io_sema);
    result = mmcedrv_close_handle(fhi.file[file_handle].id);
    if (!result) { fhi.file[file_handle].id = -1; fhi.file[file_handle].size = 0;
        memset(cache_sectors, 0, sizeof(cache_sectors)); }
    SignalSema(mmce_io_sema);
    return result;
}


//---------------------------------------------------------------------------
// FHI export #4
u32 fhi_size(int file_handle)
{
    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;

    DPRINTF("%s(%d)\n", __func__, file_handle);

    return fhi.file[file_handle].size / 512;
}

//---------------------------------------------------------------------------
// FHI export #5
int fhi_read(int file_handle, void *buffer, unsigned int sector_start, unsigned int sector_count)
{
    int res = 0;
    int retries = 0;
    unsigned int i, started, elapsed;

    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;
    if (!buffer || !sector_count || sector_count > 0x7fffffffU / 512 ||
        fhi.file[file_handle].id < 0 ||
        (unsigned long long)sector_start + sector_count > fhi.file[file_handle].size / 512)
        return 0;
    if (file_handle == FHI_FID_CDVD && ((sector_start | sector_count) & 3)) return 0;

    DPRINTF("%s(%i, %u, 0x%p, %u)\n", __func__, file_handle, (unsigned int)sector_start, buffer, sector_count);

    started = mmce_io_clock();
    WaitSema(mmce_io_sema);
    total(&io_stats.sharing_wait_us, clock_us(started));
    started = mmce_io_clock();

    //ISO, use mmcedrv_sector_read
    if (file_handle == FHI_FID_CDVD) {

        //mmcedrv uses sector sizes of 2048
        sector_start = sector_start / 4;
        sector_count = sector_count / 4;

        do {
            res = mmcedrv_read_sector(fhi.file[file_handle].id, sector_start, sector_count, buffer);
            retries++;
        } while (res != (int)sector_count && retries < 3);

        res = res * 4;

    /* Aligned ATA reads use the same 24-bit, 2 KiB sector command as DVD.
     * 7 GiB fits; never truncate high offsets or divide smaller requests. */
    } else {
        if (file_handle == FHI_FID_ATA0) {
            for (i = 0; i < ATA_CACHE_LINES; ++i) {
                if (cache_sectors[i] && sector_start >= cache_lba[i] &&
                    sector_start - cache_lba[i] <= cache_sectors[i] &&
                    sector_count <= cache_sectors[i] - (sector_start - cache_lba[i])) {
                    memcpy(buffer, ata_cache[i] + (sector_start - cache_lba[i]) * 512, sector_count * 512);
                    ++io_stats.cache_hits; res = 1; goto read_done;
                }
            }
            if (!combined_disabled && !(sector_start & 3) && !(sector_count & 3) &&
                sector_start / 4 < 0x1000000 && sector_count / 4 < 0x1000000 &&
                sector_count / 4 <= 0x1000000 - sector_start / 4) {
                res = mmcedrv_read_sector(fhi.file[file_handle].id, sector_start / 4, sector_count / 4, buffer);
                if (res == (int)(sector_count / 4)) { ++io_stats.combined_reads; res = 1; }
                else { combined_disabled = 1; ++io_stats.errors; res = 0; }
            }
        }
        if (!res) {
            ++io_stats.legacy_reads;
            if (mmcedrv_lseek64(fhi.file[file_handle].id, (s64)sector_start * 512, 0) == (s64)sector_start * 512)
                res = mmcedrv_read(fhi.file[file_handle].id, sector_count * 512, buffer) == (int)(sector_count * 512);
        }
        if (res && file_handle == FHI_FID_ATA0 && sector_count <= ATA_CACHE_BYTES / 512) {
            i = cache_next++ % ATA_CACHE_LINES;
            memcpy(ata_cache[i], buffer, sector_count * 512);
            cache_lba[i] = sector_start; cache_sectors[i] = sector_count;
        }
    }
read_done:
    elapsed = clock_us(started);
    total(&io_stats.read_us, elapsed);
    if (elapsed > io_stats.max_read_us) io_stats.max_read_us = elapsed;
    if (!res) ++io_stats.errors;
    SignalSema(mmce_io_sema);

    if (retries == 3) {
        DPRINTF("%s: Failed to read after 3 retires, sector: %u, count: %u, buffer: 0x%p\n", __func__, sector_start, sector_count, buffer);
    }

    return res;
}

//---------------------------------------------------------------------------
// FHI export #6
int fhi_write(int file_handle, const void *buffer, unsigned int sector_start, unsigned int sector_count)
{
    int res;

    if (file_handle < 0 || file_handle >= FHI_MAX_FILES)
        return 0;
    if (!buffer || !sector_count || sector_count > 0x7fffffffU / 512 ||
        fhi.file[file_handle].id < 0 ||
        (unsigned long long)sector_start + sector_count > fhi.file[file_handle].size / 512)
        return 0;

    //Don't allow writing to DVD
    if (file_handle == FHI_FID_CDVD) {
        return 0;
    }

    DPRINTF("%s(%i, %u, 0x%p, %u)\n", __func__, file_handle, (unsigned int)sector_start, buffer, sector_count);

    WaitSema(mmce_io_sema);
    if (file_handle == FHI_FID_ATA0) invalidate(sector_start, sector_count);
    res = -1;
    if (mmcedrv_lseek64(fhi.file[file_handle].id, (s64)sector_start * 512, 0) == (s64)sector_start * 512)
        res = mmcedrv_write(fhi.file[file_handle].id, sector_count * 512, buffer);
    SignalSema(mmce_io_sema);

    if (res != (int)(sector_count * 512))
        return 0;

    return 1;
}

int fhi_get_io_stats(void *buffer, unsigned int bytes)
{
    if (!buffer || bytes != sizeof(io_stats) || mmce_io_sema < 0) return -22;
    WaitSema(mmce_io_sema);
    memcpy(buffer, &io_stats, sizeof(io_stats));
    total(&((struct mmce_io_stats *)buffer)->sharing_wait_us, mmce_sio2_sharing_us);
    SignalSema(mmce_io_sema);
    return 0;
}
