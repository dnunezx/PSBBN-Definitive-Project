#ifndef PSBBN_SUITE_LOGIC_H
#define PSBBN_SUITE_LOGIC_H
#include <stdint.h>
#define SUITE_MAX_SECTORS 64
#define SUITE_SECTORS 16384U

static inline unsigned char suite_byte(uint32_t lba, unsigned int offset)
{
    return (unsigned char)((lba >> ((offset & 3) * 8)) ^ offset ^ 0xa5);
}

static inline unsigned int suite_first_mismatch(const unsigned char *data, uint32_t lba, unsigned int size)
{
    unsigned int i;
    for (i = 0; i < size; ++i)
        if (data[i] != suite_byte(lba + i / 512, i % 512)) return i;
    return size;
}

static inline int suite_range_valid(uint32_t sectors, uint32_t lba, uint32_t count)
{
    return count && count <= SUITE_MAX_SECTORS && lba < sectors && count <= sectors - lba;
}
#endif
