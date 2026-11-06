#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include "real_checks.h"
_Static_assert(offsetof(struct real_checks, image_size) == 16, "profile ABI");
_Static_assert(offsetof(struct real_checks, samples) == 32, "sample ABI");
static struct real_checks original, work;
static unsigned int length;
static void reseal(void)
{
    work.body_crc = ~real_crc_update(~0U, (const unsigned char *)&work + 16, length - 16);
}
int main(int argc, char **argv)
{
    FILE *f;
    assert(argc == 2);
    f = fopen(argv[1], "rb"); assert(f);
    length = fread(&original, 1, sizeof(original), f);
    assert(feof(f)); fclose(f);
    work = original; assert(real_checks_valid(&work, length));
    assert(work.files[0].size && work.files[1].size);
    work = original; assert(!real_checks_valid(&work, 0));
    work = original; assert(!real_checks_valid(&work, length - 1));
    work = original; work.magic[0] ^= 1; assert(!real_checks_valid(&work, length));
    work = original; work.samples[0].crc ^= 1; assert(!real_checks_valid(&work, length));
    work = original; work.sample_count = 129; assert(!real_checks_valid(&work, length));
    work = original; work.samples[0].lba = REAL_SECTORS; reseal(); assert(!real_checks_valid(&work, length));
    work = original; work.samples[0].count = 0; reseal(); assert(!real_checks_valid(&work, length));
    work = original; work.samples[0].count = 65; reseal(); assert(!real_checks_valid(&work, length));
    work = original; work.samples[0].lba = REAL_SECTORS - 1; work.samples[0].count = 2;
    reseal(); assert(!real_checks_valid(&work, length));
    work = original; work.file_count = 1; reseal(); assert(!real_checks_valid(&work, length));
    puts("Real-image profile: valid input, corruption, truncation and bounds tests passed");
    return 0;
}
