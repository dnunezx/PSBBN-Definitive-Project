/* SPDX-License-Identifier: MIT
 * Copy-on-write image transaction, shared by IOP and failure-injection tests.
 * Staging never changes the image. A verified READY footer authorizes replay.
 */
#ifndef PSBBN_COLLECTION_TXN_H
#define PSBBN_COLLECTION_TXN_H
#include <stdint.h>
#include <string.h>
#define CT_PAGE 4096U
#define CT_RECORD 4128U
#define CT_MAX_RECORDS 32768U
#define CT_MAX_PAGES 4096U
#define CT_MAGIC 0x31544350U
#define CT_READY 0x59445243U
#define CT_DONE 0x454e4443U
struct ct_io {
    void *context;
    int (*read)(void *, int, uint64_t, void *, unsigned int);
    int (*write)(void *, int, uint64_t, const void *, unsigned int);
    int (*length)(void *, int, uint64_t *);
    int (*resize)(void *, int, uint64_t);
    int (*reopen)(void *, int);
};
struct ct_index { uint32_t page, record; };
struct ct_state {
    struct ct_io io;
    uint64_t bytes;
    uint32_t identity, records, pages, active, failed;
    struct ct_index index[CT_MAX_PAGES];
    unsigned char buffer[CT_RECORD];
    unsigned char scratch[CT_PAGE];
};
static uint32_t ct_crc(const void *data, unsigned int size)
{
    const unsigned char *p = data;
    uint32_t crc = ~0U;
    unsigned int i, j;
    for (i = 0; i < size; ++i) {
        crc ^= p[i];
        for (j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static void ct_header(struct ct_state *s, uint32_t h[8], uint32_t magic, uint32_t count)
{
    memset(h, 0, 32); h[0] = magic; h[1] = 1; h[2] = s->bytes; h[3] = s->bytes >> 32;
    h[4] = s->identity; h[5] = count; h[7] = ct_crc(h, 28);
}
static int ct_header_valid(struct ct_state *s, const uint32_t h[8], uint32_t magic, uint32_t count)
{
    return h[0] == magic && h[1] == 1 && h[2] == (uint32_t)s->bytes &&
        h[3] == (uint32_t)(s->bytes >> 32) && h[4] == s->identity &&
        h[5] == count && h[6] == 0 && h[7] == ct_crc(h, 28);
}
static int ct_record_read(struct ct_state *s, unsigned int number)
{
    uint32_t *h = (uint32_t *)s->buffer;
    if (s->io.read(s->io.context, 1, 32ULL + (uint64_t)number * CT_RECORD, s->buffer, CT_RECORD)) return -1;
    return h[0] == CT_MAGIC && h[1] == number && (uint64_t)h[2] * CT_PAGE + CT_PAGE <= s->bytes &&
        h[3] == CT_PAGE && h[4] == ct_crc(s->buffer + 32, CT_PAGE) &&
        h[5] == 0 && h[6] == 0 && h[7] == ct_crc(h, 28) ? 0 : -1;
}
/* Return 1 if a committed transaction was recovered; 0 if none; -1 blocks boot.
 * Partial staging or footer writes never authorize image writes. */
static int ct_recover(struct ct_state *s)
{
    uint64_t length, offset;
    uint32_t header[8], footer[8], done[8], n, i, j, page;
    if (s->io.length(s->io.context, 1, &length)) return -1;
    if (length < 64) return 0;
    if (length > 32ULL + (uint64_t)CT_MAX_RECORDS * CT_RECORD + 64) return -1;
    n = (uint32_t)((length - 32) / CT_RECORD);
    if (n > CT_MAX_RECORDS) return -1;
    offset = 32ULL + (uint64_t)n * CT_RECORD;
    if (length < offset + 32 || length > offset + 64) return 0;
    if (s->io.read(s->io.context, 1, offset, footer, 32) || footer[0] != CT_READY) return -1;
    if (!ct_header_valid(s, footer, CT_READY, n) ||
        s->io.read(s->io.context, 1, 0, header, 32) || !ct_header_valid(s, header, CT_MAGIC, 0)) return -1;
    if (length == offset + 64 && !s->io.read(s->io.context, 1, offset + 32, done, 32) &&
        ct_header_valid(s, done, CT_DONE, n)) return 0;
    /* Verify the entire staged stream before touching a single image page. */
    s->pages = 0;
    for (i = 0; i < n; ++i) {
        if (ct_record_read(s, i)) return -1;
        page = ((uint32_t *)s->buffer)[2];
        for (j = 0; j < s->pages && s->index[j].page != page; ++j) {}
        if (j == s->pages) {
            if (s->pages == CT_MAX_PAGES) return -1;
            s->index[j].page = page; ++s->pages;
        }
        s->index[j].record = i;
    }
    for (i = 0; i < n; ++i) {
        if (ct_record_read(s, i)) return -1;
        if (s->io.write(s->io.context, 0, (uint64_t)((uint32_t *)s->buffer)[2] * CT_PAGE,
                        s->buffer + 32, CT_PAGE)) return -1;
    }
    if (s->io.reopen(s->io.context, 0)) return -1;
    /* Repeated pages supersede earlier records; verify each final page. */
    for (i = 0; i < s->pages; ++i) {
        if (ct_record_read(s, s->index[i].record) || s->io.read(s->io.context, 0,
            (uint64_t)s->index[i].page * CT_PAGE, s->scratch, CT_PAGE) || memcmp(s->scratch, s->buffer + 32, CT_PAGE)) return -1;
    }
    ct_header(s, done, CT_DONE, n);
    if (s->io.write(s->io.context, 1, offset + 32, done, 32) || s->io.reopen(s->io.context, 1) ||
        s->io.read(s->io.context, 1, offset + 32, header, 32) || memcmp(header, done, 32)) return -1;
    return 1;
}
static int ct_begin(struct ct_state *s)
{
    uint32_t h[8];
    s->records = s->pages = s->failed = s->active = 0;
    if (ct_recover(s) < 0) return -1;
    s->records = s->pages = s->failed = s->active = 0;
    if (s->io.resize(s->io.context, 1, 0)) return -1;
    ct_header(s, h, CT_MAGIC, 0);
    if (s->io.write(s->io.context, 1, 0, h, 32)) return -1;
    s->active = 1; return 0;
}
static int ct_read_page(struct ct_state *s, uint32_t page, void *data)
{
    unsigned int i;
    for (i = 0; i < s->pages; ++i) if (s->index[i].page == page) {
        if (ct_record_read(s, s->index[i].record)) return -1;
        memcpy(data, s->buffer + 32, CT_PAGE); return 0;
    }
    return s->io.read(s->io.context, 0, (uint64_t)page * CT_PAGE, data, CT_PAGE);
}
static int ct_sectors(struct ct_state *s, uint32_t lba, unsigned int count, void *data, int write)
{
    unsigned char *page = s->scratch, *p = data;
    uint64_t at = (uint64_t)lba * 512;
    unsigned int bytes, part, skip, i;
    uint32_t number, *h;
    if (!count || count > 8192 || at > s->bytes || (uint64_t)count * 512 > s->bytes - at || s->failed) {
        if (write) s->failed = 1;
        return -1;
    }
    if (write && !s->active) return -1;
    bytes = count * 512;
    while (bytes) {
        number = at / CT_PAGE; skip = at % CT_PAGE; part = CT_PAGE - skip; if (part > bytes) part = bytes;
        if (ct_read_page(s, number, page)) goto failed;
        if (write) {
            for (i = 0; i < s->pages && s->index[i].page != number; ++i) {}
            if (s->records == CT_MAX_RECORDS || (i == s->pages && s->pages == CT_MAX_PAGES)) goto failed;
            memcpy(page + skip, p, part); memset(s->buffer, 0, 32); memcpy(s->buffer + 32, page, CT_PAGE);
            h = (uint32_t *)s->buffer; h[0] = CT_MAGIC; h[1] = s->records; h[2] = number;
            h[3] = CT_PAGE; h[4] = ct_crc(page, CT_PAGE); h[7] = ct_crc(h, 28);
            if (s->io.write(s->io.context, 1, 32ULL + (uint64_t)s->records * CT_RECORD, s->buffer, CT_RECORD)) goto failed;
            if (i == s->pages) { s->index[i].page = number; ++s->pages; }
            s->index[i].record = s->records++;
        } else memcpy(p, page + skip, part);
        p += part; at += part; bytes -= part;
    }
    return 0;
failed:
    s->failed = 1; return -1;
}
static int ct_commit(struct ct_state *s)
{
    uint32_t footer[8], check[8];
    unsigned int i;
    uint64_t at = 32ULL + (uint64_t)s->records * CT_RECORD;
    if (!s->active || s->failed) return -1;
    if (s->io.reopen(s->io.context, 1)) return -1;
    for (i = 0; i < s->records; ++i) if (ct_record_read(s, i)) return -1;
    ct_header(s, footer, CT_READY, s->records);
    if (s->io.write(s->io.context, 1, at, footer, 32) || s->io.reopen(s->io.context, 1) ||
        s->io.read(s->io.context, 1, at, check, 32) || memcmp(footer, check, 32)) return -1;
    s->active = 0;
    if (ct_recover(s) < 0) return -1;
    s->pages = 0; return 0;
}
#endif
