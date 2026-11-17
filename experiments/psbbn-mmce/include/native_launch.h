/* SPDX-License-Identifier: MIT */
#ifndef PSBBN_NATIVE_LAUNCH_H
#define PSBBN_NATIVE_LAUNCH_H
#include <stdio.h>
#include "mmce_catalog.h"
static uint32_t native_crc32(const void *data, unsigned int size)
{
    const unsigned char *p = data;
    uint32_t crc = ~0U;
    unsigned int i, bit;
    for (i = 0; i < size; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static void native_game_label(const struct mmce_catalog_entry *game, char label[33])
{
    snprintf(label, 33, "PP.MMCE.%08x.%s", (unsigned int)native_crc32(game->path, strlen(game->path)), game->id);
}
/* A missing or ambiguous selection must never select the first game instead. */
static const struct mmce_catalog_entry *native_find_game(const struct mmce_catalog *catalog, const char *label)
{
    const struct mmce_catalog_entry *found = NULL;
    char candidate[33];
    unsigned int i;
    if (catalog->count > MMCE_CATALOG_MAX) return NULL;
    for (i = 0; i < catalog->count; ++i) {
        native_game_label(&catalog->entries[i], candidate);
        if (!strcmp(label, candidate)) {
            if (found) return NULL;
            found = &catalog->entries[i];
        }
    }
    return found;
}
static unsigned int native_game_match_count(const struct mmce_catalog *catalog, const char *label)
{
    char candidate[33]; unsigned int i, count = 0;
    if (catalog->count > MMCE_CATALOG_MAX) return MMCE_CATALOG_MAX + 1;
    for (i = 0; i < catalog->count; ++i) {
        native_game_label(&catalog->entries[i], candidate);
        if (!strcmp(label, candidate)) ++count;
    }
    return count;
}
static uint32_t native_le32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
/* Reject segments overlapping the running utility/heap or native EE kernel.
 * Neutrino v1.8.0 itself lives near 29 MiB, above this utility at 16 MiB. */
static int native_elf_valid(const unsigned char *data, unsigned int size,
                            uint32_t reserved_start, uint32_t reserved_end)
{
    unsigned int i, count, stride, at, loaded = 0, entry_ok = 0;
    uint32_t entry, offset, address, filesz, memsz, flags;
    if (reserved_start >= reserved_end || reserved_end > 0x1f7e000 ||
        size < 52 || memcmp(data, "\177ELF\1\1\1", 7) || data[16] != 2 || data[17] ||
        data[18] != 8 || data[19] || native_le32(data + 20) != 1) return 0;
    entry = native_le32(data + 24); at = native_le32(data + 28);
    stride = data[42] | ((unsigned int)data[43] << 8);
    count = data[44] | ((unsigned int)data[45] << 8);
    if (stride != 32 || !count || count > 32 || at > size || count * stride > size - at) return 0;
    for (i = 0; i < count; ++i, at += stride) {
        if (native_le32(data + at) != 1) continue;
        offset = native_le32(data + at + 4); address = native_le32(data + at + 8);
        filesz = native_le32(data + at + 16); memsz = native_le32(data + at + 20);
        flags = native_le32(data + at + 24);
        if (!memsz || filesz > memsz || offset > size || filesz > size - offset ||
            address < 0x100000 || address >= 0x1f7e000 || memsz > 0x1f7e000 - address ||
            (address < reserved_end && reserved_start < address + memsz)) return 0;
        if ((flags & 1) && entry >= address && entry - address < filesz) entry_ok = 1;
        ++loaded;
    }
    return loaded && entry_ok;
}
#endif
