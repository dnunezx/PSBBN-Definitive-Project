# Pinned storage sources

Hardware v0.6.12 fixes local MMCEFHI's SIO2MAN table selection. The SDK exports
modern v2.7 and legacy v1.2 tables with the same runtime name; a first-name
lookup selected the legacy alias, bypassing shared-bus locks for our v2 PADMAN
and MCMAN callers. Version-aware lookup prefers v2, retains legacy fallback,
and teardown restores the exact hooked table. Tests exercise the production
hook/relink code and reject the former selection. The user reports a successful
v0.6.12 boot on physical PS2; controller input and repeat boots remain unconfirmed.
Existing licenses are unchanged.

Hardware v0.6.10 adds two local TGE SBIOS repairs found by the separate PCSX2
boot test. `scmd.c` retains OPENCONFIG's validated block count and bounds the
READCONFIG copy to 15 bytes per block, matching PS2SDK and Linux's 45-byte OSD
buffer. `sifrpc.c` ignores completions with a null client or no outstanding
packet, preventing a null free in interrupt context. Legacy IOP replies need not
echo packet addresses. Host regression fixtures exercise the production handlers;
the separate emulator reaches interactive English PSBBN network setup. These
changes retain TGE's existing license and are included in the user-reported
successful physical PS2 boot.

The suite reuses Neutrino's ATA implementation; no second emulator is added.
All vendor sources are isolated inside this experiment.

- `vendor/atad`: Neutrino `iop/atad_emu` and `iop/common/mprintf.h`, commit
  `1bff25bcfa02e192f5b35cadbe53648786813974` from
  https://github.com/ps2max32/neutrino. AFL 3.0; see included license.
- `vendor/common`: that Neutrino commit's FHI and configuration headers.
- `vendor/mmcefhi`: MMCE FHI branch, commit
  `8f6c52c02da04a312509a654b12afba27ed437ae` from
  https://github.com/rickgaiser/mmceman/tree/fhi. MIT and retained PS2SDK AFL 2.0
  file notices; both license texts are included.

Local adaptations:

1. Separate Makefiles place generated objects inside `build/` and use current
   PS2SDK rules. Neutrino's local ATAD header precedes SDK compatibility aliases.
2. `fhi_fileid.h` forwards to the reviewed Neutrino `fhi_fileid_config.h`; the
   packed six-file configuration is unchanged (80 bytes).
3. MMCE export-table helper functions follow the table, as required by current
   PS2SDK `iopfixup`. The MMCE imports include `memcpy` for current GCC output.
4. The upstream FHI semaphore was never created. Local `mmce_fhi_init/deinit`
   creates/deletes it; module startup checks initialization before exporting FHI.

ATA transfer logic is unchanged. The diagnostic helper only invokes read paths,
validates request bounds, and reports data for independent byte comparison.
The matched modules are built from these sources and embedded in the suite ELF.
See `SUITE-MODULE-SHA256SUMS` in the download for the built module hashes.

## v0.5 read-only real-image drivers

- `vendor/{apa,libapa,pfs,libpfs}`: PS2SDK HDD source at
  `0f8c56505148ac42fc1f4158d21aa0f61141d905`, AFL 2.0. Original notices are
  retained. `Makefile.preflight` builds against the pinned SDK's headers/tools.
- APA uses ATAD, OSD partition access and no DEV9 imports. Its preflight journal
  restore accepts only an existing clean journal, without writing/resetting it.
- PFS rejects dirty/invalid journals and added subpartitions; mount propagates
  journal errors. The preflight never replays or resets a journal.
- `atad_ro.irx` builds the existing Neutrino source with `PSBBN_READ_ONLY`:
  sector writes are rejected, device/buffer/ranges validated, FHI read failures
  propagated. `atad_emu.irx` used by v0.4 retains the upstream transfer path.

`REAL-MODULE-SHA256SUMS` identifies these separately built modules. The host
mock tests compile the actual guarded ATAD source; they do not model hardware.

## v0.6 Linux boot sources

- Published PSBBN kernel commit `80b14105512ed6114c1defa8e9dee40679072874`
  is fetched/archived by `kernel/prepare.ps1`; it stays in ignored `.upstream/`
  and `build/`. GPL 2.0. `kernel/build.sh` adds the MMCE block driver, disables
  physical IDE, applies the Slim RPC IRQ patch and preserves the shipped keymap.
  v0.6.1 also disables `CONFIG_PS2_SD` and `CONFIG_SOUND` for sound-free recovery.
- `vendor/tge` and `vendor/intrelay` come from kernelreloaded/Pcsx2-lnx commit
  `eb5a2a23dc9894322aea571242365460241eb9f6`. Original TGE AFL 2.0 notices and
  license are retained. Changes are isolated build configuration and Makefiles;
  no upstream UI, networking setup or fake extra-memory setting is used.
- `vendor/sdrdrv` is PS2SDK source at the existing pinned review revision.
  It enables obsolete callbacks/EECB compatibility for TGE, imports libsd 1.4,
  and returns unsupported for four 1.5-only RPC commands. The packaged SDK
  SDRDRV requires libsd 1.5 while the packaged libsd exports 1.4; it is not used.
  Hardware v0.6 stalled while starting the adapted SDRDRV, after libsd loaded.
  v0.6.1 omits both modules; no internal SDRDRV cause or sound fix is claimed.
- `atad_checked.irx` enables bounds/error checking and propagates both FHI read
  and write results. The read-only variant still rejects writes. MMCEFHI now
  checks seek results before either non-DVD transfer, preventing a failed seek
  from accessing the previous file offset.
  FHI export 7 is a local ATA0-handle close operation, using the pinned MMCE
  close protocol. The boot preflight closes its persistent read-only handle
  after PFS unmount before reopening the image read/write; a failed close blocks
  boot. Host tests cover seek failures and close success/failure semantics.

`BOOT-MODULE-SHA256SUMS` and `KERNEL-SBIOS-SHA256SUMS` identify the boot build.
See `BOOT.md` for build instructions and explicit untested-hardware status.

## v0.6.2 early handoff diagnostics

- `kernel/early-boot.patch` instruments the pinned kernel entry, CPU probe,
  PROM and start_kernel path. `kernel/instrument_boot.py` regenerates that patch
  from unchanged pinned source; the old compiler container needs no Python.
- `include/boot_trace.h` supplies a local MIT-licensed, register-only early
  framebuffer renderer shared by the launcher and kernel. The kernel copy of
  PS2SDK's byte font is in `kernel/boot_trace_font.c` (original AFL 2.0 notice).
- Handoff exception vectors now show registers on a dedicated 4 KiB stack
  at physical 0x01f7e000..0x01f7f000, instead of silently parking. Loader ELF
  bounds reserve this area. Linux replaces the temporary vectors normally.
- Sound remains disabled. The `romcons` boot argument is omitted to avoid
  routing early output through unvalidated firmware serial/debug paths.

## v0.6.4 display correction

The SDK debug printer permits 40 rows even though its scissor is only 224
pixels (28 rows). H2 fell off the v0.6.3 screen, invalidating the H1-based
cache-call localization. v0.6.4 clears the page before handoff, tests early
output before shutdown and moves the persistent marker to scanline 160.
`boot_trace.h` now sends two-quadword A+D packets with bounded normal GIF DMA,
matching the SDK's working transport, instead of CPU writes to the GIF FIFO.
Packets use their own aligned cache line and uncached stores. No SBIOS calls
or heap allocation are needed. This changes the kernel's renderer too.

## v0.6.5 privilege-entry fix

Disassembly of the hardware-tested v0.6.4 ELF shows LW from the KSEG0 kernel
header at 0x01002684 and in the DIntr call's delay slot 0x01002690; the first
kernel-mode Status write occurs only at 0x010026a0. The user saw H2, but no H3.
`psbbn_enter_linux` is now a seven-instruction assembly gate: DI, sync.p,
LUI/Status write, sync.p, direct jump to the non-inlined privileged C body, NOP.
The IOP address is preserved in a0. No data access or C prologue precedes the
mode change. Binary audit and mutation tests enforce the gate and caller.

## v0.6.6 early Linux console

v0.6.5 reached K5 on hardware, confirming progress past the privilege bug.
`kernel/boot_trace.c` now registers a persistent early printk console before
start_kernel. Bounded numbered lines and partial-line rendering use the
validated early framebuffer path. `early-boot.patch` adds major initialization
progress messages and disables the callback before ps2con_startup changes GS
ownership. That stop only changes state, avoiding console_sem recursion while
the native console may hold the semaphore. Persistent callback/state memory is
not freed with init sections. Host mocks compile the actual console source and
cover split/partial lines, CR, wrap, bounds, repeat init, stop and render errors.

## v0.6.7 initializer tracing

The kernel's built-in __initcall macro retains each original slot and wraps its
function with a call to psbbn_run_initcall. That helper logs the function name
and returned status, calling the original exactly once. Module builds keep the
upstream behavior. The ELF audit checks all built-in slots target their named
wrappers; host tests check call count and preservation of an error return.
Native GS takeover now leaves K6 visible before silencing the early renderer.
The normal kswapd "Disabling the Out Of Memory Killer" message seen in v0.6.6
does not prove that kswapd is where initialization stalled.

## v0.6.8 graphical boot

Launcher command line adds `graphics` to bypass the observed K6 native text
console takeover. The existing graphics branch records current CRT mode using
res=-1, whose setcrtmode path returns before hardware reset/programming.
graphics_setup now returns 1 to consume the kernel option. A successful
ps2gs_open silences early framebuffer output before userspace gets access,
showing K7 first; failed opens leave logging active. This changes only display
boot routing and diagnostic ownership, not the MMCE storage path or image.

## v0.6.9 kernel assembler correction

The v0.6.8 panic decodes to ps2cdvd_init, with SW at 0x801c12f0 using $at
without materializing the block-size table's address. The same assembly run
through legacy gas also corrupts another global-store expansion. Current gas
preserves the address construction. The historical kernelloader install.txt
documents this class of broken legacy-toolchain output on newer hosts:
https://github.com/rickgaiser/kernelloader/blob/master/install.txt

kernel/Dockerfile now copies PS2 binutils 2.45.1 and its musl runtime from the
same SDK digest as the EE launcher build. GCC 2.95.2 still generates kernel
assembly. The adapter translates -mcpu=r5900/-mips2 to -march=r5900 and selects
-32; LD uses elf32lr5900. The compiler defines _MIPS_SIM_ABI32, so N32 output
is explicitly rejected. R5900-specific .set mips1/mips3 directives become
.set arch=r5900 where needed for MMI/FPU instructions under current gas.
The built source assembly, legacy/current CDVD objects and disassembly are
retained under ignored build/linux/ for regression analysis. Only current
objects are linked into the kernel. Binary checks execute the three global
table-store instructions symbolically with unknown caller-clobbered registers
and verify addresses/sector sizes/read-ahead values; the failing v0.6.8 kernel
is rejected at its actual fault. CD/DVD code is retained, not disabled.
