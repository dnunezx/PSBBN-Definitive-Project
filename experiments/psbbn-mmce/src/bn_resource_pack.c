/* Image-side Linux o32 hook. No Sony code or assets are embedded in this source. */
#include <stddef.h>
#include <stdint.h>

#define BN_SLOTS 30u
#define BN_HEADER (16u + BN_SLOTS * 16u)
#define BN_MAX_ASSET 17552u

struct bn_pack_stats {
    uint32_t magic, version, entered, done, packed, fallback, failed, empty;
    uint32_t allocation_failed, header_valid, scratch_bytes, bytes_verified;
};

struct bn_pack_api {
    void *(*allocate)(size_t, size_t);
    void (*release)(void *);
    int (*decode)(void *, unsigned int, int);
    int (*loose)(const char *, int);
    void (*finish)(void);
};

static uint32_t load32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t checksum(const unsigned char *p, uint32_t size)
{
    uint32_t value = 2166136261u, i;
    for (i = 0; i < size; ++i) value = (value ^ p[i]) * 16777619u;
    return value;
}

/* Shared with the native host harness: fallback is always the original loader. */
void bn_pack_run(const unsigned char *pack, uint32_t length,
                 const char *const *paths, int *ids,
                 volatile struct bn_pack_stats *stats, const struct bn_pack_api *api)
{
    uint32_t i, j, valid;
    unsigned char *scratch;
    volatile uint32_t *words = (volatile uint32_t *)stats;
    for (i = 0; i < sizeof(*stats) / sizeof(uint32_t); ++i) words[i] = 0;
    stats->magic = 0x504e424eu; /* NBNP, little endian */
    stats->version = 1;
    stats->entered = 1;
    valid = length >= BN_HEADER && load32(pack) == 0x41504e42u &&
            load32(pack + 4) == 0x31304b43u && load32(pack + 8) == length &&
            load32(pack + 12) == BN_SLOTS; /* BNPACK01 */
    stats->header_valid = valid;
    scratch = valid ? (unsigned char *)api->allocate(16, BN_MAX_ASSET) : NULL;
    stats->allocation_failed = valid && !scratch;
    stats->scratch_bytes = scratch ? BN_MAX_ASSET : 0;
    for (i = 0; i < BN_SLOTS; ++i) {
        uint32_t offset = 0, size = 0, expected = 0, reserved = 1;
        int id = 0;
        if (!paths[i][0]) {
            ids[i] = 0;
            ++stats->empty;
            continue;
        }
        if (scratch) {
            const unsigned char *entry = pack + 16 + i * 16;
            offset = load32(entry);
            size = load32(entry + 4);
            expected = load32(entry + 8);
            reserved = load32(entry + 12);
            if (!reserved && offset >= BN_HEADER && !(offset & 15) &&
                size >= 64 && size <= BN_MAX_ASSET && offset <= length &&
                size <= length - offset && load32(pack + offset) == 0x324d4954u &&
                checksum(pack + offset, size) == expected) {
                /* The original decoder receives writable, 16-byte-aligned input. */
                for (j = 0; j < size; ++j) scratch[j] = pack[offset + j];
                stats->bytes_verified += size;
                id = api->decode(scratch, size, 0);
                if (id) ++stats->packed;
            }
        }
        if (!id) {
            ++stats->fallback;
            id = api->loose(paths[i], 0);
        }
        if (!id) ++stats->failed;
        ids[i] = id;
    }
    if (scratch) api->release(scratch);
    api->finish();
    stats->done = 1;
}

#ifndef BN_PACK_HOST
#ifndef BN_PACK_SIZE
#error BN_PACK_SIZE must match the verified embedded pack
#endif

/* GOT slots resolve imports at runtime; fixed code addresses are baseline-only. */
static void *allocate(size_t alignment, size_t size)
{
    return (*(void *(**)(size_t, size_t))(uintptr_t)0x100d8f1c)(alignment, size);
}

static void release(void *p)
{
    (*(void (**)(void *))(uintptr_t)0x100d9794)(p);
}

static int decode(void *p, unsigned int size, int flag)
{
    return ((int (*)(void *, unsigned int, int))(uintptr_t)0x005b2c18)(p, size, flag);
}

static int loose(const char *path, int flag)
{
    return ((int (*)(const char *, int))(uintptr_t)0x005b29e8)(path, flag);
}

static void finish(void)
{
    ((void (*)(void))(uintptr_t)0x004b6100)();
}

void bn_pack_entry(void)
{
    static const struct bn_pack_api api = {allocate, release, decode, loose, finish};
    bn_pack_run((const unsigned char *)(uintptr_t)0x00981000, BN_PACK_SIZE,
                (const char *const *)(uintptr_t)0x10000dd0,
                (int *)(uintptr_t)0x1015e0b0,
                (volatile struct bn_pack_stats *)(uintptr_t)0x009a0000, &api);
}
#endif
