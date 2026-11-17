/* SPDX-License-Identifier: MIT
 * Versioned scan cache. Separate file paths preserve duplicate game IDs.
 * A failed/incomplete scan must never be passed to the cache writer.
 */
#ifndef PSBBN_MMCE_CATALOG_H
#define PSBBN_MMCE_CATALOG_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define MMCE_CATALOG_MAX 800U
#define MMCE_CATALOG_PATH 240U
#define MMCE_CATALOG_MAGIC 0x31434d50U
struct mmce_catalog_entry {
    char path[MMCE_CATALOG_PATH];
    char id[12];
    uint32_t size_lo, size_hi;
    unsigned char mtime[8];
    uint32_t media;
};
struct mmce_catalog {
    uint32_t magic, version, generation, slot, count, record_size, crc, reserved;
    struct mmce_catalog_entry entries[MMCE_CATALOG_MAX];
};
_Static_assert(sizeof(struct mmce_catalog_entry) == 272, "scan cache record ABI");
_Static_assert(offsetof(struct mmce_catalog, entries) == 32, "scan cache header ABI");
static uint32_t mmce_catalog_crc(const struct mmce_catalog *c)
{
    const unsigned char *bytes = (const unsigned char *)c;
    uint32_t crc = ~0U;
    unsigned int i, j, total = 32 + c->count * sizeof(c->entries[0]);
    for (i = 0; i < total; ++i) {
        crc ^= i >= 24 && i < 28 ? 0 : bytes[i];
        for (j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static int mmce_catalog_id_valid(const char id[12])
{
    unsigned int i;
    if (id[4] != '_' || id[8] != '.' || id[11]) return 0;
    for (i = 0; i < 11; ++i) {
        if (i == 4 || i == 8) continue;
        if (i < 4 ? (id[i] < 'A' || id[i] > 'Z') : (id[i] < '0' || id[i] > '9')) return 0;
    }
    return 1;
}
static int mmce_catalog_path_valid(const char path[MMCE_CATALOG_PATH], unsigned int media)
{
    unsigned int i, begin = media == 0 ? 4 : 3;
    if (media > 1 || !memchr(path, 0, MMCE_CATALOG_PATH) ||
        strncmp(path, media == 0 ? "DVD/" : "CD/", begin)) return 0;
    for (i = begin; path[i]; ++i)
        if ((unsigned char)path[i] < 32 || (unsigned char)path[i] >= 127 ||
            path[i] == '/' || path[i] == '\\' || path[i] == ':' || path[i] == '|') return 0;
    return i > begin + 4 && path[i - 4] == '.' &&
        (path[i - 3] == 'i' || path[i - 3] == 'I') &&
        (path[i - 2] == 's' || path[i - 2] == 'S') &&
        (path[i - 1] == 'o' || path[i - 1] == 'O');
}
static int mmce_catalog_valid(const struct mmce_catalog *c, unsigned int bytes, unsigned int slot)
{
    unsigned int i;
    if (bytes < 32 || c->magic != MMCE_CATALOG_MAGIC || c->version != 1 ||
        c->slot != slot || slot > 1 || c->reserved || c->count > MMCE_CATALOG_MAX ||
        c->record_size != sizeof(c->entries[0]) || bytes != 32 + c->count * sizeof(c->entries[0])) return 0;
    if (mmce_catalog_crc(c) != c->crc) return 0;
    for (i = 0; i < c->count; ++i) {
        if (!mmce_catalog_id_valid(c->entries[i].id) ||
            !mmce_catalog_path_valid(c->entries[i].path, c->entries[i].media) ||
            (!c->entries[i].size_lo && !c->entries[i].size_hi)) return 0;
        if (i && strcmp(c->entries[i - 1].path, c->entries[i].path) >= 0) return 0;
    }
    return 1;
}
static int mmce_catalog_same_file(const struct mmce_catalog_entry *a, const struct mmce_catalog_entry *b)
{
    static const unsigned char zero[8];
    return !strcmp(a->path, b->path) && a->media == b->media &&
        a->size_lo == b->size_lo && a->size_hi == b->size_hi &&
        memcmp(a->mtime, zero, 8) && !memcmp(a->mtime, b->mtime, 8);
}
/* Call only after validation establishes strictly sorted, terminated paths. */
static inline const struct mmce_catalog_entry *mmce_catalog_find_path(const struct mmce_catalog *c, const char *path)
{
    unsigned int low = 0, high = c->count, middle;
    int order;
    if (high > MMCE_CATALOG_MAX || !path) return NULL;
    while (low < high) {
        middle = low + (high - low) / 2;
        order = strcmp(path, c->entries[middle].path);
        if (!order) return &c->entries[middle];
        if (order < 0) high = middle;
        else low = middle + 1;
    }
    return NULL;
}
static int mmce_catalog_newer(uint32_t a, uint32_t b)
{ return (int32_t)(a - b) > 0; }
#endif
