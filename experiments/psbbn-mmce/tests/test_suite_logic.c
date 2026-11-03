#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "suite_logic.h"

int main(int argc, char **argv)
{
    unsigned char data[SUITE_MAX_SECTORS * 512];
    unsigned int i, n, positions[] = {0, 511, 512, sizeof(data) - 1};
    assert(suite_byte(0, 0) == 0xa5 && suite_byte(0, 1) == 0xa4);
    assert(suite_byte(0x01020304, 0) == 0xa1 && suite_byte(0x01020304, 1) == 0xa7);
    assert(suite_byte(0x3fff, 0) == 0x5a && suite_byte(0x3fff, 1) == 0x9b);
    for (i = 0; i < sizeof(data); ++i) data[i] = suite_byte(100 + i / 512, i % 512);
    assert(suite_first_mismatch(data, 100, sizeof(data)) == sizeof(data));
    for (n = 0; n < sizeof(positions) / sizeof(positions[0]); ++n) {
        i = positions[n];
        data[i] ^= 1;
        assert(suite_first_mismatch(data, 100, sizeof(data)) == i);
        data[i] ^= 1;
    }
    assert(suite_first_mismatch(data, 99, sizeof(data)) < sizeof(data));
    memset(data, 0xcc, sizeof(data));
    assert(suite_first_mismatch(data, 100, sizeof(data)) < sizeof(data));
    assert(suite_range_valid(SUITE_SECTORS, 0, 64));
    assert(suite_range_valid(SUITE_SECTORS, SUITE_SECTORS - 1, 1));
    assert(!suite_range_valid(SUITE_SECTORS, SUITE_SECTORS - 1, 2));
    assert(!suite_range_valid(SUITE_SECTORS, SUITE_SECTORS, 1));
    assert(!suite_range_valid(SUITE_SECTORS, 0, 0));
    assert(!suite_range_valid(SUITE_SECTORS, 0, 65));
    assert(!suite_range_valid(SUITE_SECTORS, UINT32_MAX, 1));
    assert(!suite_range_valid(SUITE_SECTORS, 0, UINT32_MAX));
    assert(!suite_range_valid(0, 0, 1));
    if (argc == 2) {
        FILE *disk = fopen(argv[1], "rb");
        unsigned int lba;
        assert(disk);
        for (lba = 0; lba < SUITE_SECTORS; lba += SUITE_MAX_SECTORS) {
            assert(fread(data, 1, sizeof(data), disk) == sizeof(data));
            assert(suite_first_mismatch(data, lba, sizeof(data)) == sizeof(data));
        }
        assert(fgetc(disk) == EOF);
        fclose(disk);
        puts("Generated disk: all 8 MiB match the actual C validator");
    }
    puts("Suite validation: corruption, stale data and boundary checks passed");
    return 0;
}
