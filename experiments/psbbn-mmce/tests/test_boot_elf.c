#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "boot_elf.h"
int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *data, *copy;
    long size;
    struct boot_elf_header *h;
    struct boot_elf_segment *p;
    unsigned int i, index = 0;
    assert(argc == 2);
    f = fopen(argv[1], "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); size = ftell(f); assert(size > 0);
    rewind(f); data = malloc(size); copy = malloc(size); assert(data && copy);
    assert(fread(data, 1, size, f) == (size_t)size); fclose(f);
    assert(boot_elf_valid(data, size));
    assert(!boot_elf_valid(data, 30));
    memcpy(copy, data, size); copy[5] = 2; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); h = (void *)copy; h->phoff = size - 1; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); h = (void *)copy; h->entry = 0x81000000; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); h = (void *)copy; p = (void *)(copy + h->phoff);
    for (i = 0; i < h->phnum; ++i) if (p[i].type == 1) { index = i; break; }
    p[index].paddr = p[index].vaddr = 0x81000000; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); p = (void *)(copy + ((struct boot_elf_header *)copy)->phoff);
    p[index].paddr = p[index].vaddr = 0x80001000; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); p = (void *)(copy + ((struct boot_elf_header *)copy)->phoff);
    p[index].filesz = p[index].memsz + 1; assert(!boot_elf_valid(copy, size));
    memcpy(copy, data, size); p = (void *)(copy + ((struct boot_elf_header *)copy)->phoff);
    p[index].offset = size; assert(!boot_elf_valid(copy, size));
    free(data); free(copy);
    puts("Boot ELF: compiled kernel accepted; protected memory, invalid entry, truncation and corrupt segments rejected");
    return 0;
}
