/* SPDX-License-Identifier: MIT
 * Recover language from the reserved Linux boot-information page.
 * Preserve the native kernel's video, audio, version and timezone settings.
 */
#ifndef PSBBN_NATIVE_CONSOLE_H
#define PSBBN_NATIVE_CONSOLE_H
#include <stdint.h>
#define NATIVE_CONSOLE_MARKER 0x4f534431U
#define NATIVE_CONSOLE_LANGUAGE_MASK 0x001f0010U
#ifndef NATIVE_CONSOLE_PAGE
#define NATIVE_CONSOLE_PAGE ((const unsigned char *)0x21fff000U)
#endif
static inline uint32_t native_console_u32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline unsigned int native_console_language(uint32_t word)
{ return ((word >> 13) & 7) ? (word >> 16) & 31 : (word >> 4) & 1; }
static inline uint32_t native_console_language_only(uint32_t current, uint32_t language_bits)
{ return (current & ~NATIVE_CONSOLE_LANGUAGE_MASK) | (language_bits & NATIVE_CONSOLE_LANGUAGE_MASK); }
/* 2: original console language; 1: older launcher's language; 0: invalid.
 * Output stays unchanged on invalid data. Early Japanese kernels use bit 4.
 */
static inline int native_console_restore(const unsigned char page[108], uint32_t current, uint32_t *output)
{
    uint32_t original = native_console_u32(page + 8), marker = native_console_u32(page + 12);
    unsigned int language = page[36]; int source = 1;
    *output = current;
    if (native_console_u32(page + 44) != 0x50324c42U || native_console_u32(page + 48) != 108 ||
        native_console_u32(page + 52) != 0x80001000U || native_console_u32(page + 56) != 0x01fff000U)
        return 0;
    if (marker == (original ^ NATIVE_CONSOLE_MARKER)) {
        if (((original >> 13) & 7) > 2 || ((original >> 16) & 31) > 7) return 0;
        language = native_console_language(original); source = 2;
    } else if (original || marker || language > 7) return 0;
    *output = native_console_language_only(current, (uint32_t)language << 16 | (language ? 0x10U : 0));
    return source;
}
#endif
