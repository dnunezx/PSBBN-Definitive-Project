/* SPDX-License-Identifier: MIT
 * Early text for the SDK debug framebuffer: bounded GIF DMA, no BIOS or heap.
 * Usable from the modern EE compiler and the kernel's GCC 2.95.2.
 */
#ifndef PSBBN_BOOT_TRACE_H
#define PSBBN_BOOT_TRACE_H
#ifndef PSBBN_TRACE_FONT
#define PSBBN_TRACE_FONT msx
#endif
extern const unsigned char PSBBN_TRACE_FONT[];

#ifdef PSBBN_TRACE_HOST
extern int psbbn_trace_send(unsigned long long value, unsigned long long reg);
#define PSBBN_TRACE_PTR(p) (p)
#else
#define PSBBN_TRACE_PTR(p) ((__typeof__(p))((unsigned long)(p) | 0x80000000UL))
static int psbbn_trace_wait(void)
{
    unsigned int limit = 1000000;
    while (*(volatile unsigned int *)0xb000a000 & 0x100)
        if (!--limit) return 0;
    return 1;
}
static int psbbn_trace_send(unsigned long long value, unsigned long long reg)
{
    /* Use the same normal GIF DMA path as the proven SDK screen printer.
     * Give the uncached packet a whole private cache line: no dirty cached
     * stack neighbor may overwrite it while DMA is reading the packet.
     */
    static unsigned long long packet[8] __attribute__((aligned(64)));
    volatile unsigned long long *q = (volatile unsigned long long *)
        (((unsigned long)packet & 0x1fffffffUL) | 0xa0000000UL);
    if (!psbbn_trace_wait()) return 0;
    q[0] = 0x1000000000008001ULL; q[1] = 0xe;
    q[2] = value; q[3] = reg;
    __asm__ __volatile__("sync.l" : : : "memory");
    *(volatile unsigned int *)0xb000a020 = 2;
    *(volatile unsigned int *)0xb000a010 = (unsigned long)packet & 0x1fffffffUL;
    *(volatile unsigned int *)0xb000a000 = 0x101;
    return psbbn_trace_wait();
}
#endif

static __inline__ int psbbn_trace_row(const char *message, unsigned int row)
{
    const unsigned char *font = PSBBN_TRACE_PTR(PSBBN_TRACE_FONT + 0);
    const char *text = PSBBN_TRACE_PTR(message);
    unsigned int col, x, y, bits;
    if (row > 216) return 0;
    /* Match init_scr's frame 0, 640x224. Drawing points needs no texture. */
    if (!psbbn_trace_send(0xa0000, 0x4c) ||
        !psbbn_trace_send(0, 0x18) ||
        !psbbn_trace_send((223ULL << 48) | (639ULL << 16), 0x40) ||
        !psbbn_trace_send(1, 0x1a) || !psbbn_trace_send(0, 0x47) ||
        !psbbn_trace_send(0, 0x45) || !psbbn_trace_send(0, 0x22) ||
        !psbbn_trace_send(6, 0) || !psbbn_trace_send(0, 1) ||
        !psbbn_trace_send((unsigned long long)(row * 16) << 16, 5) ||
        !psbbn_trace_send(((unsigned long long)((row + 8) * 16) << 16) | (640 * 16), 5) ||
        !psbbn_trace_send(0, 0) || !psbbn_trace_send(0x00ffff, 1)) return 0;
    for (col = 0; col < 78 && text[col]; ++col) {
        for (y = 0; y < 8; ++y) {
            bits = font[(unsigned char)text[col] * 8 + y];
            for (x = 0; x < 8; ++x)
                if (bits & (128U >> x))
                    if (!psbbn_trace_send(((unsigned long long)((row + y) * 16) << 16) |
                                          ((8 + col * 8 + x) * 16), 5)) return 0;
        }
    }
    /* Restore the SDK printer's coordinate and depth-test configuration. */
    return psbbn_trace_send((30976ULL << 32) | 27648, 0x18) &&
           psbbn_trace_send(0x70000, 0x47);
}

static __inline__ void psbbn_trace(const char *text)
{
    (void)psbbn_trace_row(text, 160);
}

static __inline__ void psbbn_trace_hex(const char *label, unsigned int value, unsigned int row)
{
    char line[79];
    const char *s = PSBBN_TRACE_PTR(label);
    unsigned int i = 0, shift, digit;
    while (i < 66 && s[i]) { line[i] = s[i]; ++i; }
    line[i++] = ' ';
    for (shift = 28; ; shift -= 4) {
        digit = (value >> shift) & 15;
        line[i++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        if (!shift) break;
    }
    line[i] = 0;
    (void)psbbn_trace_row(line, row);
}
#endif
