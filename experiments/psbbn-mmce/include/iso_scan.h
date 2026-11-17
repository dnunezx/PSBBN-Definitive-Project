/* SPDX-License-Identifier: MIT
 * Small, bounded ISO9660/SYSTEM.CNF reader shared by the EE and host tests.
 */
#ifndef PSBBN_ISO_SCAN_H
#define PSBBN_ISO_SCAN_H
#include <stdint.h>
#include <string.h>

#define PSBBN_ISO_IO (-1)
#define PSBBN_ISO_FORMAT (-2)
#define PSBBN_ISO_UNSUPPORTED (-3)
#define PSBBN_ISO_NOT_PS2 (-4)
struct psbbn_iso_reader {
    void *context;
    uint64_t bytes;
    int (*read_at)(void *, uint64_t, void *, unsigned int);
};
struct psbbn_iso_extent { uint32_t lba, bytes; unsigned int flags; };

static unsigned int psbbn_iso_le16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static uint32_t psbbn_iso_le32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int psbbn_iso_both16(const unsigned char *p)
{ return p[0] == p[3] && p[1] == p[2]; }
static int psbbn_iso_both32(const unsigned char *p)
{ return p[0] == p[7] && p[1] == p[6] && p[2] == p[5] && p[3] == p[4]; }
static unsigned char psbbn_iso_upper(unsigned char c)
{ return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
static int psbbn_iso_equal(const unsigned char *p, unsigned int n, const char *name)
{
    unsigned int i;
    if (strlen(name) != n) return 0;
    for (i = 0; i < n; ++i) if (psbbn_iso_upper(p[i]) != (unsigned char)name[i]) return 0;
    return 1;
}
static int psbbn_iso_read(struct psbbn_iso_reader *r, uint64_t at, void *data, unsigned int bytes)
{
    if (at > r->bytes || bytes > r->bytes - at) return PSBBN_ISO_FORMAT;
    return r->read_at(r->context, at, data, bytes) ? PSBBN_ISO_IO : 0;
}
static int psbbn_iso_record(const unsigned char *p, unsigned int n,
                            uint64_t volume, struct psbbn_iso_extent *out)
{
    uint64_t at;
    if (n < 34 || p[0] != n || 33U + p[32] > n ||
        !psbbn_iso_both32(p + 2) || !psbbn_iso_both32(p + 10) ||
        !psbbn_iso_both16(p + 28) || psbbn_iso_le16(p + 28) != 1)
        return PSBBN_ISO_FORMAT;
    if (p[1] || p[26] || p[27] || (p[25] & 0x80)) return PSBBN_ISO_UNSUPPORTED;
    out->lba = psbbn_iso_le32(p + 2); out->bytes = psbbn_iso_le32(p + 10); out->flags = p[25];
    at = (uint64_t)out->lba * 2048;
    if (at > volume || out->bytes > volume - at) return PSBBN_ISO_FORMAT;
    return 0;
}
static int psbbn_iso_find(struct psbbn_iso_reader *r, struct psbbn_iso_extent dir,
                          uint64_t volume, const char *name, struct psbbn_iso_extent *out)
{
    unsigned char block[2048] __attribute__((aligned(64)));
    unsigned int at, used, n, namesize, j;
    int found = 0, rv;
    struct psbbn_iso_extent item;
    if (!(dir.flags & 2) || !dir.bytes || dir.bytes > 4U * 1024 * 1024) return PSBBN_ISO_FORMAT;
    for (at = 0; at < dir.bytes; at += used) {
        used = dir.bytes - at; if (used > sizeof(block)) used = sizeof(block);
        rv = psbbn_iso_read(r, (uint64_t)dir.lba * 2048 + at, block, used);
        if (rv) return rv;
        for (j = 0; j < used; j += n) {
            n = block[j];
            if (!n) {
                while (j < used) if (block[j++]) return PSBBN_ISO_FORMAT;
                break;
            }
            if (n > used - j) return PSBBN_ISO_FORMAT;
            rv = psbbn_iso_record(block + j, n, volume, &item);
            if (rv) return rv;
            namesize = block[j + 32];
            if (namesize >= 2 && block[j + 33 + namesize - 2] == ';' && block[j + 33 + namesize - 1] == '1') namesize -= 2;
            if (psbbn_iso_equal(block + j + 33, namesize, name)) {
                if (found) return PSBBN_ISO_FORMAT;
                *out = item; found = 1;
            }
        }
    }
    return found ? 0 : PSBBN_ISO_NOT_PS2;
}
static int psbbn_iso_boot_path(const unsigned char *cnf, unsigned int bytes, char path[256])
{
    unsigned int i = 0, begin, end, p, n, k, found = 0;
    while (i < bytes) {
        begin = i;
        while (i < bytes && cnf[i] != '\r' && cnf[i] != '\n') ++i;
        end = i;
        while (i < bytes && (cnf[i] == '\r' || cnf[i] == '\n')) ++i;
        while (begin < end && (cnf[begin] == ' ' || cnf[begin] == '\t')) ++begin;
        p = begin;
        while (p < end && cnf[p] != '=' && cnf[p] != ' ' && cnf[p] != '\t') ++p;
        if (!psbbn_iso_equal(cnf + begin, p - begin, "BOOT2")) continue;
        while (p < end && (cnf[p] == ' ' || cnf[p] == '\t')) ++p;
        if (p == end || cnf[p++] != '=') return PSBBN_ISO_FORMAT;
        if (++found != 1) return PSBBN_ISO_FORMAT;
        while (p < end && (cnf[p] == ' ' || cnf[p] == '\t')) ++p;
        while (end > p && (cnf[end - 1] == ' ' || cnf[end - 1] == '\t')) --end;
        n = end - p;
        if (n < 8 || n >= 256) return PSBBN_ISO_FORMAT;
        for (k = 0; k < n; ++k) {
            unsigned char c = cnf[p + k];
            if (c < 32 || c >= 127) return PSBBN_ISO_FORMAT;
            path[k] = c == '\\' ? '/' : psbbn_iso_upper(c);
        }
        path[n] = 0;
        if (!strncmp(path, "CDROM0:", 7)) p = 7;
        else if (!strncmp(path, "CDROM:", 6)) p = 6;
        else return PSBBN_ISO_FORMAT;
        if (path[p] != '/') return PSBBN_ISO_FORMAT;
        while (path[p] == '/') ++p;
        if (n < p + 3 || path[n - 2] != ';' || path[n - 1] != '1') return PSBBN_ISO_FORMAT;
        path[n - 2] = 0;
        memmove(path, path + p, n - p - 1);
    }
    return found ? 0 : PSBBN_ISO_NOT_PS2;
}
static int psbbn_iso_game_id(struct psbbn_iso_reader *r, char id[12])
{
    unsigned char block[2048] __attribute__((aligned(64)));
    unsigned char cnf[4096] __attribute__((aligned(64)));
    struct psbbn_iso_extent root = {0, 0, 0}, item;
    uint64_t volume = 0;
    unsigned int i, p, begin, length;
    int rv, primary = 0, terminated = 0;
    char path[256], part[256];
    id[0] = 0;
    if (r->bytes % 2048) return PSBBN_ISO_UNSUPPORTED;
    for (i = 16; i < 48; ++i) {
        rv = psbbn_iso_read(r, (uint64_t)i * 2048, block, sizeof(block));
        if (rv) return rv;
        if (memcmp(block + 1, "CD001\1", 6)) return PSBBN_ISO_FORMAT;
        if (block[0] == 255) { terminated = 1; break; }
        if (block[0] != 1) continue;
        if (primary++) return PSBBN_ISO_FORMAT;
        if (!psbbn_iso_both32(block + 80) || !psbbn_iso_both16(block + 128) ||
            psbbn_iso_le16(block + 128) != 2048) return PSBBN_ISO_FORMAT;
        if (!psbbn_iso_both16(block + 120) || !psbbn_iso_both16(block + 124) ||
            psbbn_iso_le16(block + 120) != 1 || psbbn_iso_le16(block + 124) != 1) return PSBBN_ISO_UNSUPPORTED;
        volume = (uint64_t)psbbn_iso_le32(block + 80) * 2048;
        if (volume < 18U * 2048 || volume > r->bytes) return PSBBN_ISO_FORMAT;
        rv = psbbn_iso_record(block + 156, block[156], volume, &root);
        if (rv) return rv;
    }
    if (!primary || !terminated) return PSBBN_ISO_FORMAT;
    rv = psbbn_iso_find(r, root, volume, "SYSTEM.CNF", &item);
    if (rv) return rv;
    if ((item.flags & 2) || !item.bytes || item.bytes > sizeof(cnf)) return PSBBN_ISO_FORMAT;
    rv = psbbn_iso_read(r, (uint64_t)item.lba * 2048, cnf, item.bytes);
    if (rv) return rv;
    rv = psbbn_iso_boot_path(cnf, item.bytes, path);
    if (rv) return rv;
    p = 0;
    do {
        begin = p;
        while (path[p] && path[p] != '/') ++p;
        length = p - begin;
        if (!length || (length == 1 && path[begin] == '.') ||
            (length == 2 && path[begin] == '.' && path[begin + 1] == '.')) return PSBBN_ISO_FORMAT;
        memcpy(part, path + begin, length); part[length] = 0;
        rv = psbbn_iso_find(r, root, volume, part, &item);
        if (rv) return rv;
        if (path[p]) {
            if (!(item.flags & 2)) return PSBBN_ISO_FORMAT;
            root = item; ++p;
            if (!path[p]) return PSBBN_ISO_FORMAT;
        } else break;
    } while (1);
    if (length != 11 || part[4] != '_' || part[8] != '.') return PSBBN_ISO_NOT_PS2;
    for (i = 0; i < 11; ++i) {
        if (i == 4 || i == 8) continue;
        if (i < 4 ? (part[i] < 'A' || part[i] > 'Z') : (part[i] < '0' || part[i] > '9')) return PSBBN_ISO_NOT_PS2;
    }
    if ((item.flags & 2) || item.bytes < 52) return PSBBN_ISO_FORMAT;
    rv = psbbn_iso_read(r, (uint64_t)item.lba * 2048, block, 52);
    if (rv) return rv;
    if (memcmp(block, "\177ELF\1\1\1", 7) || psbbn_iso_le16(block + 18) != 8) return PSBBN_ISO_NOT_PS2;
    memcpy(id, part, 12);
    return 0;
}
#endif
