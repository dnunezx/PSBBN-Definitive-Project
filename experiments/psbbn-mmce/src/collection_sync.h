/* SPDX-License-Identifier: GPL-3.0-or-later
 * CosmicScale entry layout; own only PP.MMCE.* partitions with our marker.
 * All writes occur through collection_ata's COW transaction until commit.
 */
#include <hdd-ioctl.h>
#include "collection_io.h"
#include "mmce_directory.h"
#include "perf_trace.h"
MOD(collection_ata); MOD(apa_sync); MOD(pfs_sync);
extern unsigned char collection_cover[], collection_icon[], collection_titles[];
extern unsigned int size_collection_cover, size_collection_icon, size_collection_titles;
#define SYNC_MAX_PARTS 62U
#define SYNC_MARKER_MAGIC 0x31434e53U
struct sync_marker {
    u32 magic, version, artwork_crc, format;
    struct mmce_catalog_entry game;
    u32 crc;
};
struct sync_part { char name[33]; u32 lba, flags, type; };
static struct sync_part sync_parts[SYNC_MAX_PARTS];
static unsigned int sync_part_count;
static char sync_game_labels[MMCE_CATALOG_MAX][33];
static unsigned int sync_label_count;
/* Valid only within one sync invocation, after transaction recovery. Linux is
 * not running and this code is the only writer. Keep names rather than APA
 * array indexes: removal/creation rebuilds sync_parts. Never persist snapshots.
 */
struct sync_owned { char name[33]; struct sync_marker marker; unsigned int game_index; };
static struct sync_owned sync_owned[SYNC_MAX_PARTS];
static unsigned int sync_owned_count, sync_owned_reuses, sync_mount_count, sync_marker_reads;
static struct sync_owned *sync_owned_find(const char *name)
{
    unsigned int i;
    for (i = 0; i < sync_owned_count; ++i)
        if (!strcmp(name, sync_owned[i].name)) return &sync_owned[i];
    return NULL;
}
static void sync_ownership_report(void)
{
    report("Collection ownership work: snapshots=%u reuses=%u mounts=%u marker_reads=%u\n",
           sync_owned_count, sync_owned_reuses, sync_mount_count, sync_marker_reads);
    report("Collection identity work: inventory_labels=%u\n", sync_label_count);
}
/* FILEXIO devctl caps arguments at 2048 bytes, including this 8-byte header.
 * Three complete sectors fit; four would be silently truncated by the EE RPC. */
static struct { u32 lba, size; unsigned char data[1536]; } sync_raw __attribute__((aligned(64)));
static unsigned char sync_art[131072] __attribute__((aligned(64)));
static unsigned char sync_readback[4096] __attribute__((aligned(64)));
static int sync_error(const char *operation, const char *path, int result)
{
    report("Collection: %s failed: %s (result=%d)\n", operation, path, result);
    return 0;
}
static void sync_transaction_trace(int slot)
{
    struct collection_io_diag diag;
    struct mmce_seek_diag wire;
    char device[16];
    if (!remote(14, 0, 0) || reply.result) return;
    memcpy(&diag, reply.data, sizeof(diag));
    report("Collection I/O: op=%u file=%u offset=%u:%08x requested=%u result=%d records=%u pages=%u failed=%u recovery=%u/%u\n",
        diag.operation, diag.file, diag.offset_hi, diag.offset_lo, diag.requested,
        diag.result, diag.records, diag.pages, diag.failed, diag.recovered, diag.recovery_attempted);
    if (diag.operation != COLLECTION_IO_SEEK) return;
    snprintf(device, sizeof(device), "mmce%d:", slot);
    if (fileXioDevctl(device, MMCE_SEEK_DIAG, NULL, 0, &wire, sizeof(wire)) >= 0)
        report("MMCE last seek: fd=%u whence=%u offset=%u:%08x transport=%d reply=%02x returned=%u:%08x\n",
            wire.fd, wire.whence, wire.offset_hi, wire.offset_lo, wire.transport, wire.reply,
            wire.returned_hi, wire.returned_lo);
}
static int sync_read_file(const char *path, void *data, unsigned int capacity)
{
    int fd, valid, closed;
    s64 bytes;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) return -1;
    bytes = fileXioLseek64(fd, 0, FIO_SEEK_END);
    valid = bytes >= 0 && bytes <= capacity && fileXioLseek64(fd, 0, FIO_SEEK_SET) == 0 && read_exact(fd, data, bytes);
    closed = fileXioClose(fd);
    return valid && closed >= 0 ? (int)bytes : -1;
}
static int sync_write_file(const char *path, const void *data, unsigned int bytes)
{
    int fd, n, ok = 1;
    unsigned int used = 0, chunk;
    fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return sync_error("create file", path, fd);
    while (used < bytes) {
        chunk = bytes - used; if (chunk > 4096) chunk = 4096;
        n = fileXioWrite(fd, (const unsigned char *)data + used, chunk);
        if (n <= 0 || (unsigned int)n > chunk) { ok = sync_error("write file", path, n); break; }
        used += n;
    }
    n = fileXioClose(fd);
    if (n < 0) ok = sync_error("close written file", path, n);
    if (!ok) return 0;
    fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) return sync_error("reopen written file", path, fd);
    if (fileXioLseek64(fd, 0, FIO_SEEK_END) != bytes || fileXioLseek64(fd, 0, FIO_SEEK_SET) != 0)
        ok = sync_error("verify file length/seek", path, -1);
    for (used = 0; ok && used < bytes; used += chunk) {
        chunk = bytes - used; if (chunk > sizeof(sync_readback)) chunk = sizeof(sync_readback);
        if (!read_exact(fd, sync_readback, chunk) || memcmp(sync_readback, (const unsigned char *)data + used, chunk))
            ok = sync_error("verify file contents", path, used);
    }
    n = fileXioClose(fd);
    if (n < 0) ok = sync_error("close verified file", path, n);
    return ok;
}
static int sync_partition_list(void)
{
    iox_dirent_t item;
    int fd, rv, closed;
    sync_part_count = 0;
    fd = fileXioDopen("hdd0:"); if (fd < 0) return sync_error("list partitions", "hdd0:", fd);
    while ((rv = fileXioDread(fd, &item)) > 0) {
        if (sync_part_count == SYNC_MAX_PARTS || !memchr(item.name, 0, sizeof(item.name)) || strlen(item.name) > 32) {
            fileXioDclose(fd); return sync_error("validate partition listing", "hdd0:", sync_part_count);
        }
        strcpy(sync_parts[sync_part_count].name, item.name);
        sync_parts[sync_part_count].lba = item.stat.private_5;
        sync_parts[sync_part_count].flags = item.stat.attr;
        sync_parts[sync_part_count].type = item.stat.mode;
        ++sync_part_count;
    }
    closed = fileXioDclose(fd);
    if (rv || closed < 0) return sync_error("finish partition listing", "hdd0:", rv ? rv : closed);
    return 1;
}
static void sync_label(const struct mmce_catalog_entry *game, char label[33])
{ snprintf(label, 33, "PP.MMCE.%08x.%s", crc32((const unsigned char *)game->path, strlen(game->path)), game->id); }
static int sync_find(const char *label)
{
    unsigned int i;
    for (i = 0; i < sync_part_count; ++i) if (!strcmp(label, sync_parts[i].name)) return i;
    return -1;
}
static int sync_mount(const char *label, int readonly)
{
    char path[48]; int rv;
    ++sync_mount_count;
    snprintf(path, sizeof(path), "hdd0:%s", label);
    rv = fileXioMount("pfs0:", path, readonly ? FIO_MT_RDONLY : FIO_MT_RDWR);
    return rv == 0 ? 1 : sync_error(readonly ? "mount read-only" : "mount writable", path, rv);
}
static int sync_marker_read(struct sync_marker *marker)
{
    ++sync_marker_reads;
    int length = sync_read_file("pfs0:/res/mmce.bin", marker, sizeof(*marker));
    return length == sizeof(*marker) && marker->magic == SYNC_MARKER_MAGIC && marker->version == 1 &&
        marker->format == 1 && marker->crc == crc32((const unsigned char *)marker, sizeof(*marker) - 4) &&
        mmce_catalog_path_valid(marker->game.path, marker->game.media) && mmce_catalog_id_valid(marker->game.id);
}
static void sync_title(const struct mmce_catalog_entry *game, char title[128], char publisher[96])
{
    const unsigned char *line = collection_titles, *end = collection_titles + size_collection_titles, *p, *q;
    unsigned int n, i;
    p = (const unsigned char *)strrchr(game->path, '/'); p = p ? p + 1 : (const unsigned char *)game->path;
    n = strlen((const char *)p); if (n >= 4) n -= 4; if (n > 127) n = 127;
    memcpy(title, p, n); title[n] = 0; strcpy(publisher, "Unknown");
    while (line < end && (unsigned int)(end - line) > 12) {
        p = line; while (p < end && *p != '\n') ++p;
        if (!memcmp(line, game->id, 11) && line[11] == '|') {
            q = line + 12; n = 0;
            while (q < p && *q != '|' && n < 127) title[n++] = *q++;
            title[n] = 0;
            while (q < p && *q != '|') ++q;
            if (q < p) { ++q; n = 0; while (q < p && *q != '|' && n < 95) publisher[n++] = *q++; publisher[n] = 0; }
            break;
        }
        line = p < end ? p + 1 : end;
    }
    /* Preserve printable ASCII; a PC-side metadata/artwork preparation step
     * can supply localized text later without changing catalog identities. */
    for (i = 0; title[i]; ++i) if ((unsigned char)title[i] < 32 || (unsigned char)title[i] >= 127) title[i] = ' ';
    for (i = 0; publisher[i]; ++i) if ((unsigned char)publisher[i] < 32 || (unsigned char)publisher[i] >= 127) publisher[i] = ' ';
}
static int sync_png(const unsigned char *data, unsigned int bytes)
{
    unsigned int at = 8, n;
    if (bytes < 57 || memcmp(data, "\211PNG\r\n\032\n", 8) || memcmp(data + 12, "IHDR", 4) ||
        memcmp(data + 16, "\0\0\1\0\0\0\1\0", 8)) return 0;
    while (at + 12 <= bytes) {
        n = ((u32)data[at] << 24) | ((u32)data[at + 1] << 16) | ((u32)data[at + 2] << 8) | data[at + 3];
        if (n > bytes - at - 12) return 0;
        if (crc32(data + at + 4, n + 4) != (((u32)data[at + n + 8] << 24) |
            ((u32)data[at + n + 9] << 16) | ((u32)data[at + n + 10] << 8) | data[at + n + 11])) return 0;
        if (!memcmp(data + at + 4, "IEND", 4)) return n == 0 && at + 12 == bytes;
        at += n + 12;
    }
    return 0;
}
static unsigned int sync_cover(int slot, const char *id)
{
    char path[96]; int n;
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/covers/%s.png", slot, id);
    n = sync_read_file(path, sync_art, sizeof(sync_art));
    if (n > 0 && sync_png(sync_art, n)) return n;
    if (n > 0) report("Collection: invalid local cover %s; using default\n", id);
    memcpy(sync_art, collection_cover, size_collection_cover); return size_collection_cover;
}
static int sync_raw_write(u32 lba, const unsigned char *data, unsigned int bytes)
{
    unsigned int used = 0, n;
    int rv;
    struct { u32 lba, size; } read;
    while (used < bytes) {
        n = bytes - used; if (n > sizeof(sync_raw.data)) n = sizeof(sync_raw.data);
        memset(sync_raw.data, 0, sizeof(sync_raw.data)); memcpy(sync_raw.data, data + used, n);
        sync_raw.lba = lba; sync_raw.size = (n + 511) / 512;
        rv = fileXioDevctl("hdd0:", HDIOC_WRITESECTOR, &sync_raw, 8 + sync_raw.size * 512, NULL, 0);
        if (rv < 0) return sync_error("write OSD attributes", "hdd0:", rv);
        read.lba = lba; read.size = sync_raw.size;
        rv = fileXioDevctl("hdd0:", HDIOC_READSECTOR, &read, sizeof(read), sync_readback, read.size * 512);
        if (rv < 0) return sync_error("read OSD attributes", "hdd0:", rv);
        if (memcmp(sync_readback, sync_raw.data, read.size * 512))
            return sync_error("verify OSD attributes", "hdd0:", lba);
        used += n; lba += sync_raw.size;
    }
    return 1;
}
static int sync_entry(int slot, const struct mmce_catalog_entry *game, const char *label,
                       unsigned int cover_size, u32 cover_crc, int existing)
{
    char path[96], title[128], publisher[96], titleid[12], info[2048], icon[1024], cnf[1024];
    struct sync_marker marker;
    unsigned char attributes[512];
    u32 *table = (u32 *)attributes;
    int fd, rv, part, ok = 1, length;
    unsigned int i, j;
    int format[3] = {8192, 0x2d66, 0};
    if (!existing) {
        snprintf(path, sizeof(path), "hdd0:%s,,,8M,PFS", label);
        fd = fileXioOpen(path, FIO_O_RDWR | FIO_O_CREAT, 0);
        if (fd < 0) return sync_error("create APA partition", path, fd);
        rv = fileXioClose(fd);
        if (rv < 0) return sync_error("close new APA partition", path, rv);
        snprintf(path, sizeof(path), "hdd0:%s", label);
        rv = fileXioFormat("pfs:", path, format, sizeof(format));
        if (rv < 0) return sync_error("format PFS partition", path, rv);
    }
    if (!sync_mount(label, 0)) return 0;
    rv = fileXioMkdir("pfs0:/res", 0777);
    if (rv < 0 && rv != -17) ok = sync_error("create resource directory", "pfs0:/res", rv);
    sync_title(game, title, publisher);
    for (i = j = 0; game->id[i]; ++i) if (game->id[i] != '.') titleid[j++] = game->id[i] == '_' ? '-' : game->id[i];
    titleid[j] = 0;
    length = snprintf(info, sizeof(info),
        "title = %s\ntitle_id = %s\ntitle_sub_id = 0\nrelease_date = \ndeveloper_id = \npublisher_id = %s\n"
        "note = \ncontent_web = \nimage_topviewflag = 0\nimage_type = 0\nimage_count = 1\nimage_viewsec = 600\n"
        "copyright_viewflag = 0\ncopyright_imgcount = 0\ngenre = \nparental_lock = 1\neffective_date = 0\n"
        "expire_date = 0\nviolence_flag = 0\ncontent_type = 255\ncontent_subtype = 0\n", title, titleid, publisher);
    if (length < 0 || (unsigned int)length >= sizeof(info) || !sync_write_file("pfs0:/res/info.sys", info, length) ||
        !sync_write_file("pfs0:/res/jkt_001.png", sync_art, cover_size)) ok = 0;
    memset(&marker, 0, sizeof(marker)); marker.magic = SYNC_MARKER_MAGIC; marker.version = marker.format = 1;
    marker.game = *game; marker.artwork_crc = cover_crc;
    marker.crc = crc32((const unsigned char *)&marker, sizeof(marker) - 4);
    if (!sync_write_file("pfs0:/res/mmce.bin", &marker, sizeof(marker))) ok = 0;
    /* Keep the exact path with the entry, independently of any launch ABI. */
    snprintf(path, sizeof(path), "mmce%d:/", slot);
    snprintf(cnf, sizeof(cnf), "BOOT2 = PATINFO\nHDDUNITPOWER = NICHDD\npath = mmce%d:/PSBBN/neutrino/neutrino.elf\n"
        "titleid = %s\narg = -cwd=mmce%d:/PSBBN/neutrino\narg = -bsd=mmce\narg = -dvd=mmce%d:/%s\nskip_argv0 = 0\n",
        slot, game->id, slot, slot, game->path);
    if (!sync_write_file("pfs0:/res/system.cnf", cnf, strlen(cnf))) ok = 0;
    rv = fileXioUmount("pfs0:");
    if (rv < 0) ok = sync_error("unmount written entry", label, rv);
    if (!ok || !sync_partition_list()) return 0;
    part = sync_find(label);
    if (part < 0 || sync_parts[part].type != APA_TYPE_PFS) return sync_error("find staged partition", label, part);
    /* HDD OSD attributes are relative to partition+0x1000, matching HDL Dump. */
    memset(attributes, 0, sizeof(attributes)); memcpy(attributes, "PS2ICON3D", 9);
    table[4] = 0x200; table[5] = strlen(cnf);
    snprintf(icon, sizeof(icon), "PS2X\ntitle0=%.48s\ntitle1=%.95s\nbgcola=58\nbgcol0=0,3,43\nbgcol1=0,0,10\n"
        "bgcol2=1,0,9\nbgcol3=0,1,19\nlightdir0=1.0,-1.0,1.0\nlightdir1=-1.0,1.0,-1.0\nlightdir2=0.0,0.0,0.0\n"
        "lightcolamb=64,64,64\nlightcol0=64,64,64\nlightcol1=16,16,16\nlightcol2=0,0,0\nuninstallmes0=\nuninstallmes1=\nuninstallmes2=\n", title, publisher);
    table[6] = 0x600; table[7] = strlen(icon); table[8] = table[10] = 0xa00;
    table[9] = table[11] = size_collection_icon;
    if (strlen(cnf) > 1024 || strlen(icon) > 1024) return 0;
    rv = sync_raw_write(sync_parts[part].lba + 8, attributes, sizeof(attributes)) &&
        sync_raw_write(sync_parts[part].lba + 9, (unsigned char *)cnf, strlen(cnf)) &&
        sync_raw_write(sync_parts[part].lba + 11, (unsigned char *)icon, strlen(icon)) &&
        sync_raw_write(sync_parts[part].lba + 13, collection_icon, size_collection_icon);
    if (!rv || !sync_mount(label, 1)) return 0;
    ok = sync_marker_read(&marker) && !memcmp(&marker.game, game, sizeof(*game)) && marker.artwork_crc == cover_crc;
    if (!ok) sync_error("verify ownership marker", label, -1);
    rv = fileXioUmount("pfs0:");
    if (rv < 0) ok = sync_error("unmount verified entry", label, rv);
    return ok;
}
/* 1 = boot permitted; 0 = recovery/commit uncertain, never start Linux. */
static __attribute__((noinline)) int sync_collection(int slot, int complete)
{
    unsigned int i, j, needed = 0, reclaim = 0, additions = 0, updates = 0, removals = 0, cover_size;
    char label[33], path[48];
    struct sync_marker marker;
    u32 cover_crc, free_sectors = 0;
    int part, ok = 1, rv, mounted;
    uint64_t phase;
    sync_owned_count = sync_owned_reuses = sync_mount_count = sync_marker_reads = 0;
    sync_label_count = 0;
    report("Collection sync r8: checked staging image-seek recovery\n");
    report("Collection ownership snapshot r1: same-pass reuse\n"); save_report();
    rpc_dead = 0;
    phase = perf_begin("transaction_open_recover");
    if (!LOAD(collection_ata) || !bind_service(SUITE_RPC_ID) || !remote(10, slot, checks.body_crc)) return 0;
    perf_end("transaction_open_recover", phase, reply.result == 0);
    if (reply.result) { sync_error("initialize transaction", "MMCE image/journal", reply.result); sync_transaction_trace(slot); return 0; }
    if (!complete) {
        report("Collection: incomplete scan; catalog preserved after recovery\n");
        return remote(12, 0, 0) && reply.result == 0;
    }
    if (scan_next.count > MMCE_CATALOG_MAX) goto abort;
    for (i = 0; i < scan_next.count; ++i) {
        sync_label(&scan_next.entries[i], sync_game_labels[i]);
        ++sync_label_count;
    }
    phase = perf_begin("collection_reconcile");
    if (!LOAD(apa_sync) || !LOAD(pfs_sync) || !sync_partition_list()) goto abort;
    /* Validate owned entries and account for confirmed removals before deciding
     * whether a full catalog can accommodate replacements or renamed files. */
    for (i = 0; i < sync_part_count; ++i) {
        if (strncmp(sync_parts[i].name, "PP.MMCE.", 8) || sync_parts[i].flags || sync_parts[i].type != APA_TYPE_PFS) continue;
        if (!sync_mount(sync_parts[i].name, 1)) goto abort;
        mounted = sync_marker_read(&marker);
        if (fileXioUmount("pfs0:") < 0 || !mounted) goto abort;
        sync_label(&marker.game, label);
        if (strcmp(label, sync_parts[i].name)) goto abort;
        for (j = 0; j < scan_next.count; ++j) {
            if (!strcmp(sync_game_labels[j], sync_parts[i].name)) break;
        }
        if (sync_owned_count == SYNC_MAX_PARTS) goto abort;
        strcpy(sync_owned[sync_owned_count].name, sync_parts[i].name);
        sync_owned[sync_owned_count].marker = marker;
        sync_owned[sync_owned_count].game_index = j;
        ++sync_owned_count;
        if (j == scan_next.count) ++reclaim;
    }
    /* Reserve main-partition/preflight slots and space before staging writes. */
    for (i = 0; i < scan_next.count; ++i) {
        if (sync_find(sync_game_labels[i]) < 0) ++needed;
    }
    rv = fileXioDevctl("hdd0:", HDIOC_FREESECTOR, NULL, 0, &free_sectors, sizeof(free_sectors));
    if (rv < 0 || needed > free_sectors / 16384 + reclaim || sync_part_count + needed - reclaim >= 63) {
        report("Collection: insufficient APA space/slots; previous catalog retained\n"); goto abort;
    }
    /* Remove only positively identified owned entries after a complete scan. */
    for (i = 0; i < sync_owned_count; ++i) {
        ++sync_owned_reuses;
        if (sync_owned[i].game_index == scan_next.count) {
            snprintf(path, sizeof(path), "hdd0:%.32s", sync_owned[i].name);
            if (fileXioRemove(path) < 0) goto abort;
            ++removals;
        }
    }
    if (!sync_partition_list()) goto abort;
    for (i = 0; i < scan_next.count; ++i) {
        const struct mmce_catalog_entry *game = &scan_next.entries[i];
        strcpy(label, sync_game_labels[i]); part = sync_find(label);
        cover_size = sync_cover(slot, game->id); cover_crc = crc32(sync_art, cover_size);
        if (part >= 0) {
            struct sync_owned *owned = sync_owned_find(label);
            if (sync_parts[part].flags || sync_parts[part].type != APA_TYPE_PFS || !owned ||
                strcmp(owned->marker.game.path, game->path)) goto abort;
            ++sync_owned_reuses;
            if (!memcmp(&owned->marker.game, game, sizeof(*game)) && owned->marker.artwork_crc == cover_crc) continue;
        }
        report("Collection: %s %s -> %s\n", part >= 0 ? "updating" : "adding", game->id, label);
        if (!sync_entry(slot, game, label, cover_size, cover_crc, part >= 0)) goto abort;
        if (part >= 0) ++updates; else ++additions;
    }
    if (!additions && !updates && !removals) {
        sync_ownership_report();
        perf_end("collection_reconcile", phase, 1);
        perf_count("collection_changes", 0);
        report("Collection: unchanged; no image commit\n");
        return remote(12, 0, 0) && reply.result == 0;
    }
    perf_end("collection_reconcile", phase, 1);
    sync_ownership_report();
    perf_count("collection_additions", additions);
    perf_count("collection_updates", updates);
    perf_count("collection_removals", removals);
    phase = perf_begin("staged_boot_validation");
    /* Re-read original boot fingerprints through the staged view before seal. */
    if (!sync_mount("__system", 1)) goto abort;
    ok = check_boot_file(0, "pfs0:/p2lboot/osdboot.elf") && check_boot_file(1, "pfs0:/p2lboot/vmlinux");
    if (fileXioUmount("pfs0:") < 0 || !ok) goto abort;
    if (fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0) < 0 || !remote(13, 0, 0) || reply.result) goto abort;
    perf_end("staged_boot_validation", phase, 1);
    report("Collection: verified staging; committing %u additions, %u updates, %u removals\n", additions, updates, removals);
    save_report();
    /* READY has now authorized redo. Any failure from here blocks Linux until
     * the next startup successfully replays and verifies the transaction. */
    phase = perf_begin("collection_commit_readback");
    if (!remote(11, 0, 0) || reply.result) {
        perf_end("collection_commit_readback", phase, 0);
        report("Collection: commit incomplete; next boot will recover\n"); sync_transaction_trace(slot); return 0;
    }
    report("Collection: image commit read back; full preflight follows\n");
    perf_end("collection_commit_readback", phase, 1);
    sync_transaction_trace(slot);
    return remote(12, 0, 0) && reply.result == 0;
abort:
    sync_ownership_report();
    sync_transaction_trace(slot);
    report("Collection: staging failed; live image preserved\n");
    /* IOP reset discards all APA/PFS caches. Staging has no READY footer. */
    return remote(12, 0, 0) && reply.result == 0;
}
