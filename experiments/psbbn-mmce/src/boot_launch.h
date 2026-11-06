/* SPDX-License-Identifier: MIT
 * Included by real_image.c only for the combined boot build.
 */
#include "boot_elf.h"
#include "block_rpc.h"
#include "boot_trace.h"
MOD(atad_checked); MOD(block_rpc); MOD(intrelay);
MOD(linux_mcman); MOD(linux_mcserv); MOD(linux_padman);
extern unsigned char linux_kernel[], linux_sbios[];
extern unsigned int size_linux_kernel, size_linux_sbios;
extern void psbbn_jump_kseg(void), psbbn_flush_caches(void), psbbn_flush_tlbs(void);
extern void psbbn_enter_linux(u32 iopaddr) __attribute__((noreturn));
extern void psbbn_exception_entry(void);
static struct psbbn_block_packet block_tx __attribute__((aligned(64)));
static struct psbbn_block_packet block_rx __attribute__((aligned(64)));
struct boot_sysconf {
    short timezone;
    unsigned char aspect, date, language, spdif, summertime, time, video;
};
struct boot_info {
    u32 pccard, command, reserved[2];
    unsigned char rtc[8];
    u32 machine, pcic;
    struct boot_sysconf sysconf;
    u32 magic, size, sbios, maxmem, stringsize, stringdata, versions[10];
};
struct boot_page { struct boot_info info; char command[256]; };
static struct boot_page boot_page;
_Static_assert(offsetof(struct boot_info, magic) == 44, "Linux bootinfo ABI");

void __attribute__((noreturn)) psbbn_handoff_fault(u32 cause, u32 epc, u32 badaddr, u32 status)
{
    (void)psbbn_trace_row("HANDOFF EXCEPTION - photograph these values", 168);
    psbbn_trace_hex("CAUSE", cause, 176);
    psbbn_trace_hex("EPC", epc, 184);
    psbbn_trace_hex("BADVADDR", badaddr, 192);
    psbbn_trace_hex("STATUS", status, 200);
    for (;;) {}
}

static void install_handoff_vectors(void)
{
    u32 target = (u32)psbbn_exception_entry | 0x80000000U;
    unsigned int i;
    for (i = 0; i < 5; ++i) {
        volatile u32 *v = (volatile u32 *)(0x80000000U + i * 0x80);
        v[0] = 0x3c1a0000U | (target >> 16); /* lui k0, high */
        v[1] = 0x375a0000U | (target & 0xffff); /* ori k0, k0, low */
        v[2] = 0x03400008U; /* jr k0 */
        v[3] = 0;
    }
    psbbn_flush_caches();
}

static int block_call(int function, unsigned int lba, unsigned int count)
{
    static u32 seq;
    int rv, i, received = 64;
    memset(&block_tx, 0, 64);
    block_tx.magic = PSBBN_BLOCK_MAGIC; block_tx.version = PSBBN_BLOCK_VERSION;
    block_tx.sequence = ++seq; block_tx.lba = lba; block_tx.count = count; block_tx.bytes = count * 512;
    if (function == PSBBN_BLOCK_READ) received += count * 512;
    memset(&block_rx, 0, received);
    rv = SifCallRpc(&rpc, function, SIF_RPC_M_NOWAIT, &block_tx, 64,
                    &block_rx, received, NULL, NULL);
    if (rv < 0) return 0;
#ifdef PSBBN_EMULATOR_BUILD
    if (function == PSBBN_BLOCK_INFO)
        report("Block INFO submitted: busy=%d magic=%08x capacity=%u\n",
               SifCheckStatRpc(&rpc), block_rx.magic, block_rx.capacity);
#endif
#ifdef PSBBN_EMULATOR_BUILD
    /* The patched emulator can lose the SDK timer's sleeping thread. Keep
     * the guest running while IOP completion interrupts arrive instead. */
    for (i = 0; i < 100000000 && SifCheckStatRpc(&rpc); ++i)
        __asm__ __volatile__("nop");
#else
    for (i = 0; i < 10000 && SifCheckStatRpc(&rpc); ++i) usleep(1000);
#endif
    if (SifCheckStatRpc(&rpc)) { report("Block RPC timeout\n"); return 0; }
    return block_rx.magic == PSBBN_BLOCK_MAGIC && block_rx.version == PSBBN_BLOCK_VERSION &&
        block_rx.sequence == seq && block_rx.lba == lba && block_rx.count == count && !block_rx.result &&
        block_rx.capacity == REAL_SECTORS && block_rx.bytes == count * 512;
}

#ifndef PSBBN_EMULATOR_BUILD
static int patch_boot_handle(int slot)
{
    struct fhi_fileid *settings = NULL;
    char path[80];
    unsigned int i;
    int fd, id;
    for (i = 0; i + sizeof(*settings) <= size_mmcefhi_irx; i += 4) {
        if (*(u32 *)(mmcefhi_irx + i) == MODULE_SETTINGS_MAGIC) {
            if (settings) return 0;
            settings = (struct fhi_fileid *)(mmcefhi_irx + i);
        }
    }
    snprintf(path, sizeof(path), "mmce%d:/PSBBN/psbbn.img", slot);
    fd = fileXioOpen(path, FIO_O_RDWR, 0);
    report("Boot image open read/write: %d\n", fd);
    if (fd < 0) return 0;
    id = fileXioIoctl2(fd, 0x80, NULL, 0, NULL, 0);
    if (!settings || id < 0 || id >= 250 || fileXioLseek64(fd, 0, FIO_SEEK_END) != (s64)checks.image_size) {
        fileXioClose(fd); return 0;
    }
    memset(settings, 0, sizeof(*settings));
    settings->magic = MODULE_SETTINGS_MAGIC; settings->devNr = slot;
    settings->file[FHI_FID_ATA0].id = id; settings->file[FHI_FID_ATA0].size = checks.image_size;
    return 1;
}
#endif

static __attribute__((noinline)) void boot_copy(volatile unsigned char *dest, const unsigned char *source, u32 size)
{
    while (size--) *dest++ = *source++;
}

/* Entered only through the assembly mode-switch gate. KSEG reads in this C
 * prologue are now safe even if the compiler schedules them early.
 */
void __attribute__((noreturn, noinline)) psbbn_enter_linux_privileged(u32 iopaddr)
{
    const unsigned char *image = (const unsigned char *)((u32)linux_kernel | 0x80000000U);
    const unsigned char *sbios = (const unsigned char *)((u32)linux_sbios | 0x80000000U);
    const struct boot_elf_header *h = (const struct boot_elf_header *)image;
    const struct boot_elf_segment *segments = (const struct boot_elf_segment *)(image + h->phoff);
    u32 sbios_size = size_linux_sbios, entry = h->entry;
    unsigned int i, j;
    psbbn_trace("H3 PRIVILEGED HANDOFF ENTERED");
    psbbn_flush_caches();
    psbbn_jump_kseg();
    psbbn_flush_caches(); /* Commit stack data after changing its virtual alias. */
    install_handoff_vectors();
    psbbn_trace("H4 KSEG0 ACTIVE - COPYING BIOS");
    /* All source data and the active stack are now addressed through KSEG0. */
    for (i = 0; i < 0xf000; ++i) ((volatile unsigned char *)0x80001000)[i] = 0;
    boot_copy((void *)0x80001000, sbios, sbios_size);
    *(volatile u32 *)0x80001008 = iopaddr;
    psbbn_trace("H5 BIOS COPIED - COPYING LINUX");
    for (i = 0; i < h->phnum; ++i) {
        volatile unsigned char *dest;
        if (segments[i].type != 1) continue;
        dest = (volatile unsigned char *)segments[i].paddr;
        boot_copy(dest, image + segments[i].offset, segments[i].filesz);
        for (j = segments[i].filesz; j < segments[i].memsz; ++j) dest[j] = 0;
    }
    boot_copy((void *)0x81fff000, (void *)((u32)&boot_page | 0x80000000U), sizeof(boot_page));
    psbbn_trace("H6 LINUX AND BOOT PARAMETERS COPIED");
    *(volatile u32 *)0xb0000010 = 0; *(volatile u32 *)0xb0000810 = 0;
    *(volatile u32 *)0xb0001010 = 0; *(volatile u32 *)0xb0001810 = 0;
    *(volatile u32 *)0xb000f010 = *(volatile u32 *)0xb000f010;
    *(volatile u32 *)0xb000f000 = *(volatile u32 *)0xb000f000;
    *(volatile u32 *)0xb000e010 = *(volatile u32 *)0xb000e010;
    __asm__ __volatile__("mtc0 %0, $16\n sync.p\n" :: "r"(0x00073003U));
    __asm__ __volatile__("mtc0 $zero, $24\n mtc0 $zero, $25\n sync.p\n");
    psbbn_flush_caches();
    psbbn_flush_tlbs();
    psbbn_trace("H7 TLB CLEARED - JUMPING TO LINUX");
    ((void (*)(int, void *, void *, void *))entry)(0, NULL, (void *)0x81fff000, NULL);
    psbbn_trace("ERROR - LINUX ENTRY RETURNED");
    for (;;) {}
}

static void launch_psbbn(int slot)
{
    int i, rv, pass = 1, interrupts, trace_ok;
    unsigned int osd = 0;
    if (!boot_elf_valid(linux_kernel, size_linux_kernel) || size_linux_sbios < 16 ||
        size_linux_sbios > 0xf000 || memcmp(linux_sbios + 4, "PS2b", 4)) {
        report("Embedded kernel/SBIOS validation failed\n"); return;
    }
#ifdef PSBBN_EMULATOR_BUILD
    (void)slot;
#else
    if (!patch_boot_handle(slot)) { report("Cannot prepare writable boot image\n"); return; }
    report("Checkpoint: final IOP reset, storage bridge and Linux handoff next.\n");
    report("Linux will have read/write access to the MMCE image.\n");
    if (!save_report()) { report("USB checkpoint unavailable; stopping before boot\n"); return; }
#endif
    reset_iop();
    scr_clear(); scr_setXY(0, 0);
#ifdef PSBBN_EMULATOR_BUILD
    report("PSBBN PCSX2 TEST - COPIED VIRTUAL HDD\nMMCE transport is not exercised.\n");
    if (!LOAD(sio2man) || !LOAD(linux_mcman) || !LOAD(linux_mcserv) || !LOAD(linux_padman) ||
        !LOAD(native_dev9) || !LOAD(native_atad)) pass = 0;
#else
    report("PSBBN MMCE BOOT v0.6.12 - LINUX STARTUP\nSound disabled for this recovery test.\n");
    if (!LOAD(sio2man) || !LOAD(linux_mcman) || !LOAD(linux_mcserv) || !LOAD(linux_padman) ||
        !LOAD(mmcefhi) || !LOAD(atad_checked)) pass = 0;
#endif
    if (pass) {
        rv = -1;
        report("Starting module block_rpc (write-enabled)...\n");
        i = SifExecModuleBuffer(block_rpc_irx, size_block_rpc_irx, sizeof("--write"), "--write", &rv);
        report("Block service write-enabled: id=%d start=%d\n", i, rv);
        pass = i >= 0 && rv == 0 && bind_service(PSBBN_BLOCK_RPC_ID);
    }
    if (pass) {
        report("Checking writable storage bridge capacity...\n");
        pass = block_call(PSBBN_BLOCK_INFO, 0, 0) && (block_rx.flags & PSBBN_BLOCK_WRITABLE);
    }
    if (pass) report("Checking %u storage bridge samples...\n", checks.sample_count);
    for (i = 0; pass && i < (int)checks.sample_count; ++i) {
        struct real_sample *s = &checks.samples[i];
        pass = block_call(PSBBN_BLOCK_READ, s->lba, s->count);
#ifdef PSBBN_BOOT_BUILD
        if (pass) pass = real_session_matches(&boot_samples, i, crc32(block_rx.data, s->count * 512));
#else
        if (pass) pass = crc32(block_rx.data, s->count * 512) == s->crc;
#endif
        if (!pass) report("Boot bridge sample %d failed at LBA=%u\n", i + 1, s->lba);
        else if ((i + 1) % 16 == 0) report("Boot bridge samples: %d/%u passed\n", i + 1, checks.sample_count);
    }
    if (!pass) {
        report("Boot bridge validation failed; Linux not started\n");
        reset_iop(); if (file_environment()) save_report();
        return;
    }
    /* USB belongs to Linux after this point; do not load an IOP USB driver. */
    if (!LOAD(intrelay)) {
        report("Interrupt relay failed; Linux not started\n");
        reset_iop(); if (file_environment()) save_report();
        return;
    }
    memset(&boot_page, 0, sizeof(boot_page));
    boot_page.info.magic = 0x50324c42; boot_page.info.size = sizeof(boot_page.info);
    boot_page.info.sbios = 0x80001000; boot_page.info.maxmem = 32 * 1024 * 1024 - 4096;
    boot_page.info.command = 0x81fff000U + offsetof(struct boot_page, command);
    snprintf(boot_page.command, sizeof(boot_page.command), "root=/dev/hda1 rw console=tty0 graphics");
    GetOsdConfigParam(&osd);
    boot_page.info.sysconf.spdif = osd & 1; boot_page.info.sysconf.aspect = (osd >> 1) & 3;
    boot_page.info.sysconf.video = (osd >> 3) & 1; boot_page.info.sysconf.language = (osd >> 16) & 31;
    boot_page.info.sysconf.timezone = (osd >> 21) & 0x7ff;
    rv = SifGetReg(0x80000000);
    /* The SDK allows 40 text rows but this framebuffer shows only 28.
     * Reset the page so H2 and later diagnostics cannot fall offscreen.
     */
    scr_clear(); scr_setXY(0, 0);
#ifdef PSBBN_EMULATOR_BUILD
    report("PSBBN PCSX2 TEST - HANDOFF\n");
#else
    report("PSBBN MMCE BOOT v0.6.12 - HANDOFF\n");
#endif
    report("Bridge samples passed. Starting Linux at %08x\n", ((struct boot_elf_header *)linux_kernel)->entry);
#ifdef PSBBN_EMULATOR_BUILD
    scr_printf("Linux log is readable through the background debugger.\n");
#else
    scr_printf("USB checkpoint saved. Photograph any Linux error shown next.\n");
#endif
    report("T0 Checking early screen output\n");
    interrupts = DIntr(); ee_kmode_enter();
    trace_ok = psbbn_trace_row("T1 EARLY SCREEN OUTPUT TEST", 160);
    ee_kmode_exit(); if (interrupts) EIntr();
    if (!trace_ok) { report("Early screen DMA timed out; handoff cancelled\n"); return; }
    report("T2 Early screen transfer returned\n");
    report("H0 Closing EE services\n");
#ifdef PSBBN_EMULATOR_BUILD
    /* Host stdout uses IOP RPC and cannot be called after closing it. */
    emulator_stdout = 0;
#endif
    SifExitIopHeap(); SifLoadFileExit(); SifExitRpc(); SifStopDma();
    report("H1 EE services closed\n");
    /* The assembly gate selects kernel mode before the C body can dereference
     * KSEG pointers; the C handoff then flushes both caches directly before
     * changing aliases or overwriting the old kernel.
     */
    report("H2 Entering privileged handoff\n");
    psbbn_enter_linux(rv);
}
