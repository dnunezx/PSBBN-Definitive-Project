#ifndef PSBBN_REAL_CHECKS_H
#define PSBBN_REAL_CHECKS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define REAL_MAX_SAMPLES 128
#define REAL_SECTORS 14680064U
struct real_sample { uint32_t lba, count, crc; };
struct real_file { uint32_t size, crc; };
struct real_checks {
    unsigned char magic[8];
    uint32_t body_size, body_crc;
    uint64_t image_size;
    uint32_t sample_count, file_count;
    struct real_sample samples[REAL_MAX_SAMPLES];
    struct real_file files[2];
};
/* Booted filesystems are writable. Compare every backend with a repeated,
 * read-only FILEXIO sample taken during this launch, not factory content. */
struct real_session_samples {
    uint32_t crc[REAL_MAX_SAMPLES];
    unsigned char valid[REAL_MAX_SAMPLES];
};
static inline int real_session_learn(struct real_session_samples *session,
    unsigned int index, uint32_t first, uint32_t repeated)
{
    if (index >= REAL_MAX_SAMPLES) return 0;
    session->valid[index] = 0;
    if (first != repeated) return 0;
    session->crc[index] = first;
    session->valid[index] = 1;
    return 1;
}
static inline int real_session_matches(const struct real_session_samples *session,
    unsigned int index, uint32_t got)
{
    return index < REAL_MAX_SAMPLES && session->valid[index] && session->crc[index] == got;
}
_Static_assert(offsetof(struct real_checks, image_size) == 16, "image size ABI");
_Static_assert(offsetof(struct real_checks, samples) == 32, "sample ABI");
static inline uint32_t real_crc_update(uint32_t crc, const unsigned char *data, unsigned int length)
{
    unsigned int i, bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static inline int real_checks_valid(struct real_checks *c, unsigned int bytes)
{
    unsigned int i, expected;
    if (bytes < 32 || memcmp(c->magic, "PSBBN05\0", 8) ||
        c->image_size != (uint64_t)REAL_SECTORS * 512 ||
        c->sample_count == 0 || c->sample_count > REAL_MAX_SAMPLES || c->file_count != 2)
        return 0;
    expected = 32 + c->sample_count * 12 + 16;
    if (bytes != expected || c->body_size != bytes - 16 ||
        ~real_crc_update(~0U, (const unsigned char *)c + 16, bytes - 16) != c->body_crc)
        return 0;
    for (i = 0; i < c->sample_count; ++i) {
        struct real_sample *s = &c->samples[i];
        if (!s->count || s->count > 64 || s->lba >= REAL_SECTORS || s->count > REAL_SECTORS - s->lba)
            return 0;
    }
    memmove(c->files, (const unsigned char *)c + 32 + c->sample_count * 12, 16);
    for (i = 0; i < 2; ++i)
        if (!c->files[i].size || c->files[i].size > 32 * 1024 * 1024) return 0;
    return 1;
}
#endif
