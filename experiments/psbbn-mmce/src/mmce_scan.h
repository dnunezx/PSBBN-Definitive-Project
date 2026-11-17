/* SPDX-License-Identifier: MIT
 * Runs in the launcher's restored FILEXIO environment before Linux starts.
 * Scans the MMCE unit containing psbbn.img. Image/catalog entries are not
 * written here; this stage produces a complete, recoverable inventory first.
 */
#include <stdlib.h>
#include "iso_scan.h"
#include "mmce_catalog.h"
#include "mmce_directory.h"
#include "perf_trace.h"
static struct mmce_catalog scan_previous __attribute__((aligned(64)));
static struct mmce_catalog scan_next __attribute__((aligned(64)));
static int scan_previous_file = -1;
static int scan_is_complete;
static void scan_directory_trace(int slot)
{
    char device[16];
    struct mmce_directory_diag diag;
    snprintf(device, sizeof(device), "mmce%d:", slot);
    memset(&diag, 0, sizeof(diag));
    if (fileXioDevctl(device, MMCE_DIRECTORY_DIAG, NULL, 0, &diag, sizeof(diag)) >= 0)
        report("MMCE directory wire r%u: reads=%u packet=%u status=%u name=%u iterator=%u tail=%u result=%d\n",
            diag.revision, diag.reads, diag.stage, diag.status, diag.name_bytes, diag.iterator, diag.tail, diag.result);
}

static int scan_iso_read_at(void *context, uint64_t at, void *data, unsigned int bytes)
{
    int fd = *(int *)context;
    return fileXioLseek64(fd, at, FIO_SEEK_SET) == (s64)at && read_exact(fd, data, bytes) ? 0 : -1;
}
static int scan_load_cache(int slot, int which, struct mmce_catalog *catalog)
{
    char path[80];
    s64 bytes;
    int fd, valid = 0;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/scan-cache-%c.bin", slot, 'a' + which);
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) return 0;
    bytes = fileXioLseek64(fd, 0, FIO_SEEK_END);
    if (bytes >= 32 && bytes <= (s64)sizeof(*catalog) && fileXioLseek64(fd, 0, FIO_SEEK_SET) == 0 &&
        read_exact(fd, catalog, bytes)) valid = mmce_catalog_valid(catalog, bytes, slot);
    if (fileXioClose(fd) < 0) valid = 0;
    return valid;
}
static const struct mmce_catalog_entry *scan_known(const char *path)
{
    return mmce_catalog_find_path(&scan_previous, path);
}
static int scan_compare(const void *a, const void *b)
{ return strcmp(((const struct mmce_catalog_entry *)a)->path, ((const struct mmce_catalog_entry *)b)->path); }
static int scan_root_folders(int slot, unsigned int *present)
{
    char path[32];
    iox_dirent_t item;
    int fd, rv, complete = 1, closed;
    unsigned int media;
    *present = 0;
    snprintf(path, sizeof(path), "mmce%d:/", slot);
    fd = fileXioDopen(path);
    if (fd < 0) { report("MMCE root: open failed (%d)\n", fd); return 0; }
    while (1) {
        memset(&item, 0, sizeof(item));
        rv = fileXioDread(fd, &item); if (rv <= 0) break;
        if (!memchr(item.name, 0, sizeof(item.name))) {
            report("MMCE root: invalid entry name\n"); complete = 0; continue;
        }
        if (psbbn_iso_equal((const unsigned char *)item.name, strlen(item.name), "DVD")) media = 0;
        else if (psbbn_iso_equal((const unsigned char *)item.name, strlen(item.name), "CD")) media = 1;
        else continue;
        report("MMCE root: %s mode=%08x\n", item.name, item.stat.mode);
        if (!FIO_S_ISDIR(item.stat.mode)) {
            report("MMCE root: game-folder name is not a directory\n"); complete = 0; continue;
        }
        *present |= 1U << media;
    }
    closed = fileXioDclose(fd);
    report("MMCE root: end=%d close=%d folders=%u\n", rv, closed, *present);
    scan_directory_trace(slot);
    return complete && rv == 0 && closed >= 0;
}
static int scan_write_cache(int slot, int which)
{
    char path[80];
    int fd, result = 1, wrote;
    unsigned int offset = 0, bytes = 32 + scan_next.count * sizeof(scan_next.entries[0]), chunk;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/scan-cache-%c.bin", slot, 'a' + which);
    fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return 0;
    while (offset < bytes) {
        chunk = bytes - offset; if (chunk > 32768) chunk = 32768;
        wrote = fileXioWrite(fd, (unsigned char *)&scan_next + offset, chunk);
        if (wrote <= 0 || (unsigned int)wrote > chunk) { result = 0; break; }
        offset += wrote;
    }
    if (fileXioClose(fd) < 0) result = 0;
    /* The newest previous slot remains untouched if this write is interrupted.
     * Readback establishes logical completeness, not power-loss durability.
     */
    if (!result || !scan_load_cache(slot, which, &scan_previous)) return 0;
    return !memcmp(&scan_next, &scan_previous, bytes);
}
static int scan_folder(int slot, unsigned int media, int force,
                        unsigned int *cached, unsigned int *parsed)
{
    char directory[32], path[256];
    iox_dirent_t item;
    iox_stat_t after;
    struct mmce_catalog_entry *entry;
    const struct mmce_catalog_entry *known;
    struct psbbn_iso_reader reader;
    int dir, rv, fd, closed, complete = 1, valid;
    unsigned int length;
    snprintf(directory, sizeof(directory), "mmce%d:/%s", slot, media == 0 ? "DVD" : "CD");
    dir = fileXioDopen(directory);
    if (dir < 0) { report("MMCE scan: cannot list %s (%d)\n", directory, dir); return 0; }
    while (1) {
        memset(&item, 0, sizeof(item));
        rv = fileXioDread(dir, &item); if (rv <= 0) break;
        if (!memchr(item.name, 0, sizeof(item.name))) { complete = 0; continue; }
        if (item.name[0] == '.' || FIO_S_ISDIR(item.stat.mode)) continue;
        length = strlen(item.name);
        if (length < 5 || item.name[length - 4] != '.' ||
            psbbn_iso_upper(item.name[length - 3]) != 'I' ||
            psbbn_iso_upper(item.name[length - 2]) != 'S' ||
            psbbn_iso_upper(item.name[length - 1]) != 'O') continue;
        if (scan_next.count == MMCE_CATALOG_MAX) { complete = 0; report("MMCE scan: 800-entry limit reached\n"); break; }
        entry = &scan_next.entries[scan_next.count];
        memset(entry, 0, sizeof(*entry)); entry->media = media;
        if (length + (media == 0 ? 4 : 3) >= sizeof(entry->path)) {
            complete = 0; report("MMCE scan: filename too long\n"); continue;
        }
        memcpy(entry->path, media == 0 ? "DVD/" : "CD/", media == 0 ? 4 : 3);
        memcpy(entry->path + (media == 0 ? 4 : 3), item.name, length + 1);
        if (!mmce_catalog_path_valid(entry->path, media)) {
            complete = 0; report("MMCE scan: unsupported filename\n"); continue;
        }
        entry->size_lo = item.stat.size; entry->size_hi = item.stat.hisize;
        memcpy(entry->mtime, item.stat.mtime, 8);
        snprintf(path, sizeof(path), "mmce%d:/%s", slot, entry->path);
        known = scan_known(entry->path);
        if (!force && known && mmce_catalog_same_file(entry, known)) {
            memcpy(entry->id, known->id, sizeof(entry->id)); ++*cached;
        } else {
            fd = fileXioOpen(path, FIO_O_RDONLY, 0);
            if (fd < 0) { complete = 0; report("MMCE scan: cannot open %s (%d)\n", path, fd); continue; }
            reader.context = &fd; reader.read_at = scan_iso_read_at;
            reader.bytes = ((uint64_t)entry->size_hi << 32) | entry->size_lo;
            valid = fileXioLseek64(fd, 0, FIO_SEEK_END) == (s64)reader.bytes &&
                psbbn_iso_game_id(&reader, entry->id) == 0;
            if (fileXioClose(fd) < 0) valid = 0;
            if (!valid) { complete = 0; report("MMCE scan: unreadable/unsupported ISO %s\n", path); continue; }
            ++*parsed;
        }
        /* A copy/replacement still in progress must not publish a new catalog. */
        if (fileXioGetStat(path, &after) < 0 || after.size != entry->size_lo ||
            after.hisize != entry->size_hi || memcmp(after.mtime, entry->mtime, 8)) {
            complete = 0; report("MMCE scan: file changed during scan %s\n", path); continue;
        }
        report("MMCE game: %s %s (%u:%08x bytes)\n", entry->id, path, entry->size_hi, entry->size_lo);
        ++scan_next.count;
    }
    closed = fileXioDclose(dir);
    report("MMCE folder %s: end=%d close=%d\n", directory, rv, closed);
    scan_directory_trace(slot);
    if (rv < 0 || closed < 0) complete = 0;
    return complete;
}
static __attribute__((noinline)) void scan_mmce_games(int slot)
{
    char force_path[80];
    int first, second, force, fd, complete, target;
    unsigned int cached = 0, parsed = 0, present;
    report("\nMMCE game inventory r5: compatible directory replies (DVD/ and CD/)\n");
    report("MMCE cache lookup r2: validated sorted paths\n");
    scan_is_complete = 0;
    memset(&scan_previous, 0, sizeof(scan_previous)); memset(&scan_next, 0, sizeof(scan_next));
    first = scan_load_cache(slot, 0, &scan_previous);
    second = scan_load_cache(slot, 1, &scan_next);
    scan_previous_file = first ? 0 : -1;
    if (second && (!first || mmce_catalog_newer(scan_next.generation, scan_previous.generation))) {
        memcpy(&scan_previous, &scan_next, sizeof(scan_previous)); scan_previous_file = 1;
    }
    if (scan_previous_file < 0) memset(&scan_previous, 0, sizeof(scan_previous));
    memset(&scan_next, 0, sizeof(scan_next));
    scan_next.magic = MMCE_CATALOG_MAGIC; scan_next.version = 1; scan_next.slot = slot;
    scan_next.record_size = sizeof(scan_next.entries[0]); scan_next.generation = scan_previous.generation + 1;
    snprintf(force_path, sizeof(force_path), "mmce%d:/PSBBN/rescan.flag", slot);
    fd = fileXioOpen(force_path, FIO_O_RDONLY, 0); force = fd >= 0;
    if (fd >= 0) fileXioClose(fd);
    /* MMCEMAN reports a missing directory and transport failures alike as -1.
     * Enumerate the root successfully first; never infer removal from DOPEN.
     */
    complete = scan_root_folders(slot, &present);
    if (!complete) report("MMCE scan: root listing incomplete\n");
    if (present & 1U) { if (!scan_folder(slot, 0, force, &cached, &parsed)) complete = 0; }
    else report("MMCE scan: DVD/ absent in root listing\n");
    if (present & 2U) { if (!scan_folder(slot, 1, force, &cached, &parsed)) complete = 0; }
    else report("MMCE scan: CD/ absent in root listing\n");
    /* Confirm the MMCE unit is still accessible even when both directories
     * were absent. Missing/ejected media must never look like removals.
     */
    snprintf(force_path, sizeof(force_path), "mmce%d:/PSBBN/real-checks.bin", slot);
    fd = fileXioOpen(force_path, FIO_O_RDONLY, 0);
    if (fd < 0) complete = 0;
    else if (fileXioClose(fd) < 0) complete = 0;
    report("MMCE scan: %u games, %u cached IDs, %u ISO reads; %s\n",
           scan_next.count, cached, parsed, complete ? "COMPLETE" : "INCOMPLETE");
    perf_count("scan_games", scan_next.count);
    perf_count("scan_cached_ids", cached);
    perf_count("scan_iso_parses", parsed);
    if (!complete) {
        report("MMCE scan: keeping previous %u-game inventory; image unchanged\n", scan_previous.count);
        return;
    }
    qsort(scan_next.entries, scan_next.count, sizeof(scan_next.entries[0]), scan_compare);
    scan_next.crc = mmce_catalog_crc(&scan_next);
    if (!mmce_catalog_valid(&scan_next, 32 + scan_next.count * sizeof(scan_next.entries[0]), slot)) {
        report("MMCE scan: inventory validation failed; previous inventory retained\n"); return;
    }
    scan_is_complete = 1;
    if (scan_previous_file >= 0 && scan_next.count == scan_previous.count &&
        !memcmp(scan_next.entries, scan_previous.entries, scan_next.count * sizeof(scan_next.entries[0]))) {
        report("MMCE scan: inventory unchanged; no cache write\n"); return;
    }
    target = scan_previous_file == 0 ? 1 : 0;
    report("MMCE scan: writing cache %c generation %u\n", 'a' + target, scan_next.generation);
    if (!scan_write_cache(slot, target)) {
        report("MMCE scan: cache write/readback failed; previous cache slot retained\n"); return;
    }
    report("MMCE scan: inventory cached and read back; collection image not modified yet\n");
}
