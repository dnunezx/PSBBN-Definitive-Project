/* SPDX-License-Identifier: MIT */
#ifndef PSBBN_BOOT_ELF_H
#define PSBBN_BOOT_ELF_H
#include <stdint.h>
#include <string.h>
struct boot_elf_header {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct boot_elf_segment { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; };
static inline int boot_elf_valid(const void *data, unsigned int size)
{
    const struct boot_elf_header *h = data;
    const struct boot_elf_segment *p;
    unsigned int i, j, loads = 0, entry = 0;
    if (size < sizeof(*h) || memcmp(h->ident, "\177ELF\1\1\1", 7) ||
        h->type != 2 || h->machine != 8 || h->version != 1 || h->ehsize != 52 ||
        h->phentsize != 32 || !h->phnum || h->phnum > 16 || h->phoff % 4 ||
        h->phoff > size || h->phnum > (size - h->phoff) / 32) return 0;
    p = (const struct boot_elf_segment *)((const unsigned char *)data + h->phoff);
    for (i = 0; i < h->phnum; ++i) {
        uint32_t base;
        if (p[i].type != 1) continue;
        ++loads;
        base = p[i].paddr & 0x1fffffffU;
        if ((p[i].paddr & 0xe0000000U) != 0x80000000U || p[i].paddr != p[i].vaddr ||
            base < 0x10000 || base >= 0x800000 || !p[i].memsz || p[i].memsz > 0x800000 - base ||
            p[i].filesz > p[i].memsz || p[i].offset > size || p[i].filesz > size - p[i].offset)
            return 0;
        if (p[i].align > 1 && ((p[i].align & (p[i].align - 1)) ||
            ((p[i].offset ^ p[i].vaddr) & (p[i].align - 1)))) return 0;
        for (j = 0; j < i; ++j)
            if (p[j].type == 1 && p[i].vaddr < p[j].vaddr + p[j].memsz &&
                p[j].vaddr < p[i].vaddr + p[i].memsz) return 0;
        if ((p[i].flags & 1) && h->entry >= p[i].vaddr && h->entry - p[i].vaddr < p[i].filesz) entry = 1;
    }
    return loads && entry;
}
#endif
