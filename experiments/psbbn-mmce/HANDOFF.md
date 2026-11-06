# Current handoff — November 6, 2026

## v0.6.12: user reports successful physical PS2 boot

The user reports that v0.6.12 booted on the physical PS2. Controller input and
repeat-boot reliability have not been separately confirmed. This supersedes
the pending-hardware status in the original package. User authorized committing
and pushing the current source changes to GitHub; private images and generated
artifacts stay excluded.

After the v0.6.11 retry, the user's photo shows MMCE RPC timeout/storage
disabled, LBA 8396832 / 4096-byte read failure, root-inode read errors and VFS
root-mount panic. These errors do not prove filesystem corruption. Do not reset
or repair the card based on this photo. The USB unclaimed-device message does
not establish USB failure; image storage is MMCE.

Compared v0.6.10 and v0.6.11 embedded payloads: kernel, SBIOS, MMCEFHI, ATAD,
block RPC, SIO2MAN, MCMAN, MCSERV, PADMAN and intrelay are byte-identical.
v0.6.11 changed the launcher's live-sample handling, not those payloads.

Found a concrete existing race-enabling defect: SDK SIO2MAN registers v2.7,
then changes the v1.2 alias name to sio2man and registers it. Loadcore prepends
libraries. MMCEFHI's first-name lookup therefore hooks v1.2 while actual
PADMAN/MCMAN import v2.3 and bypass those locks. v0.6.12 adds version-aware
lookup, prefers the modern table, keeps legacy fallback, and saves the exact
hooked table for restoration. Teardown only restores loadcore/intrman hooks
when they were installed. This is a verified code defect, but causation of
the reported controller/storage symptoms remains unconfirmed on hardware.

tests/test_sio2man_hook.py compiles production ioplib and sio2man_hook sources
with small IOP mocks. UBSan run passes: both registration orders select modern
exports, actual caller stubs relink and restore, minor/major filtering works,
and legacy-only fallback works. Mutation to the old first-name init lookup
fails the expected modern-table assertion. Build audit additionally pins the
actual dual SIO2 exports and v2.3 PADMAN/MCMAN imports.

Use the v0.6.12 package's ELF only, retaining the existing image/profile.
Log is PSBBN-BOOT-v0612.log. Hardware boot is user-reported; controller success
has not been established.
Built ELF: 4,075,980 bytes; SHA256
024f4fe3a5cbfa7861d473280d8b91e34b497851adec0115d71c5ce30a0674ba.
Compared embedded payloads with v0.6.11: only MMCEFHI changed; kernel, SBIOS,
SIO2MAN, PADMAN, MCMAN, MCSERV, ATAD, block RPC and intrelay match exactly.
Hardware build, boot/module audit, CDVD binary regression and privilege-gate
mutation checks passed. v0.6.10/v0.6.11 archives remain available unchanged.

## v0.6.11 repairs repeat-boot preflight; controller remains unresolved

User reports v0.6.10 boots on the physical PS2 but controller input does not
respond. Controller type/port/screen and whether it works in the ELF launcher
were requested asynchronously; not answered yet. No controller fix is claimed.
The current TGE openSlot buffer is already 64-byte aligned at 0x8000c340.

The next attempt stopped before Linux. User-provided D:/PSBBN-BOOT-v0610.log
passes profile validation, exact 7 GiB size and samples 1..21, then sample 22
at LBA 4202496 returns CRC 681d16fa versus original 37005e89. That LBA is the
__linux.9 ext2 filesystem start. Original PC image readback confirms 37005e89;
the live card image was not examined, so filesystem corruption is not ruled out.
Normal writes during the previously successful boot can explain the mismatch.
Strict factory sample CRCs are inappropriate for normal repeat boots.

v0.6.11 boot preflight learns current sample CRCs only after two identical
read-only FILEXIO reads, then requires FHI, ATAD and the final writable bridge
to match those per-launch in-memory references. It preserves profile checksum,
capacity, APA/PFS discovery/mount and full original boot-file CRC checks. It
does not modify the image or checks file. Standalone v0.5 diagnostics retain
strict original CRCs. Host tests reproduce the logged changed-content case and
reject unstable reads, missing references and mismatched downstream reads.
Existing profile corruption/truncation/bounds tests also pass.

Hardware ELF built; boot audit, CDVD binary regression and mode-gate mutation
checks pass. Size 4,075,724 bytes; SHA256
a01aa057bd95a937356eecb9a20300227b89a6e01d3b59fc860df6dff8c3e670.
Log is PSBBN-BOOT-v0611.log. User should replace only the USB ELF, keeping the
existing image/profile. Physical confirmation of the repeat-boot fix is pending.

## Hardware v0.6.10 prepared for the next console test

Rebuilt psbbn_boot.elf with both emulator-proven SBIOS fixes, retaining the
hardware MMCE path, preflight/checkpoint gate, original kernel and disabled
sound. Version/banner is v0.6.10; USB checkpoint is PSBBN-BOOT-v0610.log.
Build and boot audit, CDVD binary regression and mode-gate mutation tests passed.
ELF: 4,073,912 bytes; SHA256
2bdd9e5945862ad92c0395e3b0a5fdb72229785fbe0fcfb3f78b2b1f6099d684.
The user copies the ELF to USB and launches it on the PS2; an unchanged existing
MMCE image/profile can stay on the card. Use the original exact-size image for
any fresh card copy, not the emulator's padded/modified image. Hardware validation
of these fixes remains pending. No card/USB files were edited by the assistant.

## PCSX2 boot reaches interactive PSBBN setup

See EMULATOR.md. Separate psbbn_emulator.elf boots a disposable 7 GiB image
copy through native virtual DEV9/PS2SDK ATAD and the existing block RPC/kernel.
It passes all 66 samples, mounts root, runs the PSBBN bn process, completes the
graphical content-management update and reaches English network setup with
Start/Back/Enter prompts. Main menu/network operation were not tested. No
mouse/keyboard/window automation is permitted; this is now a standing user rule
in global and project AGENTS.md. All later observations used background PINE
reads and the emulator's native save-state Screenshot.png.

Two SBIOS bugs were found and repaired in source and the emulator ELF:
- READCONFIG copied 1024 bytes into Linux's 45-byte OSD buffer, clearing socket
  globals. It now remembers the opened count and copies only count*15 bytes on
  success. Guarded-buffer tests pass and reject an overcopy mutation.
- RPC completion could call rpc_packet_free(NULL), panicking in interrupt
  context while bn ran. It now ignores a completion with no outstanding packet
  and null clients. Normal/duplicate/legacy-bind regression cases pass. Legacy
  IOP completion packets do not reliably echo pkt_addr; do not require equality.

Stock PCSX2 2.6.3 needs 1 MiB of image padding for the final-sector read and
stalls at Linux userspace entry. The emulator-only block service hides padding
by capping capacity/request bounds at 14680064 sectors. Both normal and capped
RPC tests pass. A separate native Windows PCSX2 build uses commit
337daf7ed9a1529da26abdcbef996407b8071145 and the existing six kernelreloaded
Linux patches, plus kernel/pcsx2-linux-signature.patch. It uses both interpreters
and disables IntcStat/WaitLoop and desktop input sources. The emulator launcher
uses bounded busy polling instead of SDK timer sleeps for its block RPC wait;
the patched emulator otherwise loses the timer's sleeping thread at INFO.

Artifacts are ignored under build/emulator: psbbn_emulator.elf,
BUILD-SHA256SUMS, linux-boot.log, psbbn-network-setup.png, psbbn-startup.png,
the portable pcsx2-patched runtime, copied test disk and native state archives.
restart-background.ps1 -Patched starts it hidden; add -FreshImage only for a
clean strict-profile retry (copies the private original and adds padding).
Read Linux output with tests/read_emulator_log.py --tail; --snapshot saves the
guest without desktop control. Use KSEG0 addresses, not unmapped user addresses,
in PINE: the latter can fault the emulator's debugger thread.

The original private PC image is preserved. Older v0.6.9 ZIPs are preserved;
the current hardware ELF is now rebuilt as v0.6.10, described above. The
Linux kernel is unchanged (SHA256 1895c02afdc130bcbb3ebbbe4d8f217278a952eb7307604ff58229795203de9f).
No commit, push or upload was performed. No proprietary files are added to Git.

## Latest: v0.6.8 CDVD panic identified; v0.6.9 repairs kernel assembly

User photo saved as ignored build/hardware-v068-kernel-panic.png shows
`Kernel panic: Attempted to kill init!`. Stack maps to ps2cdvd_init through
psbbn_run_initcall/do_initcalls. The failing instruction at 0x801c12f0 stores
through uninitialized register $at while assigning blksize_size[major].
The required address-loading instructions are missing from the old binary.
This confirms progress past the earlier native text-console takeover, but
does not establish a successful Linux MMCE root mount or PSBBN startup.

Reassembling identical production CDVD assembly with legacy and current tools
reproduces corrupt global-address expansion in the legacy tool. v0.6.9 keeps
GCC 2.95.2 but rebuilds the entire kernel with pinned PS2 binutils 2.45.1.
The assembler wrapper explicitly selects O32 and translates old CPU flags;
modern-binutils.patch keeps the R5900 ISA enabled in EE-specific assembly.
CDVD is retained. The graphical boot workaround and storage path remain.

Full kernel/launcher builds and binary audit passed. The new CDVD regression
symbolically checks all three table stores and their values in the actual
kernel; it accepts v0.6.9 and rejects v0.6.8 at the observed 0x801c12f0 fault.
O32 ABI checks, mode-gate mutation tests and kernel entry/load-range tests pass.
Kernel SHA256 `1895c02afdc130bcbb3ebbbe4d8f217278a952eb7307604ff58229795203de9f`.
ELF: 4,073,624 bytes, SHA256
`37efd95deab98f877de61374b845db9e145b46a98e438e82b3b4307c5f7ed654`.
Artifacts: psbbn-mmce-boot-v0.6.9.zip and corresponding -source.zip.
User copies USB psbbn_boot.elf themselves; no PC image/card edits made.
Next step is hardware boot with v0.6.9; if it fails, capture final screen and
PSBBN-BOOT-v069.log. PSBBN UI is not yet confirmed. No commit/push/upload.

## Latest: K6 reported; v0.6.8 selects graphical boot to bypass takeover

User reports yellow K6 NATIVE GS CONSOLE TAKEOVER on v0.6.7; no new photo/log
provided. This identifies entry into native ps2con_startup, whose subsequent
reset/mode/DMA operation is not individually known. The user is frustrated by
repeated diagnostic builds and asked to reach PSBBN itself. Do not claim the
GUI boots or a deeper GS fault is repaired without hardware evidence.

v0.6.8 adds the kernel's existing `graphics` command-line option, skipping
take_over_console. The alternate branch calls ps2gs_crtmode with res=-1,
which returns in setcrtmode before GS reset/CRT hardware programming, preserving
the current display. Fixed graphics_setup to return 1 so checksetup consumes
the option rather than passing a stray "graphics" argv entry to /sbin/init.
The early log remains active during kernel/root setup. A successful ps2gs_open
prints K7 USERSPACE GRAPHICS DEVICE OPENED and silences it before userspace can
use GS, preventing log rendering over the graphical UI. Failed opens leave
the log active. Existing storage bridge, sound-disabled recovery, privilege
gate and initializer logging are retained. This is a targeted K6 workaround,
not a confirmed repair of the internal native text-console fault.

Full kernel/ELF builds, binary audit, mode-gate mutations and ELF bounds tests
passed. An extracted-production-function C fixture verifies graphics_setup
consumes its option, graphics mode bypasses takeover and preserves current CRT
mode, and the text path is retained. Host console tests cover silencing after
userspace GS handoff. Production patched ps2con source is copied to ignored
build/linux/ps2con-boot-source.c by the kernel build for that test.
Kernel SHA256 `01c16a5b8b50370d00e8551a15918f06c51b7cba1e33252e9778a82558836334`.
ELF: 4,635,800 bytes, SHA256
`aa9a46e5d45bc07bc7ea5c294a52a44dbc2d40a2ef1a68d624cb10494acb1fe1`.
Artifacts: psbbn-mmce-boot-v0.6.8.zip and corresponding -source.zip.
User copies USB psbbn_boot.elf themselves; SD image/profile unchanged by us.
Next hardware result should show continued numbered Linux lines past the text
console, a later specific initializer/partition/root error, K7, or the PSBBN UI.
Return PSBBN-BOOT-v068.log and final screen if it fails. No v0.6.8 hardware
result, confirmed PSBBN UI, commit/push/upload exists.

## Latest: v0.6.6 Linux initcalls reached; v0.6.7 names each initializer

User photo saved as ignored build/hardware-v066-linux-log.png. The numbered
early log wraps: newest 0048 RT netlink, 0049 Starting kswapd, 004A Disabling
the Out Of Memory Killer. Earlier visible lines confirm cache/memory setup,
PS2 DMA, SIF, RTC driver and power setup and NET4.0. The OOM-killer message is
normal conditional printk in mm/vmscan.c:kswapd_init, not an OOM failure.
Later initializers are silent; native GS takeover also silences early output.
Do not assert that the last printed line identifies the failed initializer.
No confirmed MMCE Linux block initialization, root mount, init or PSBBN UI yet.

v0.6.7 preserves each built-in __initcall slot and wraps its function with
psbbn_run_initcall, logging `PSBBN init: NAME` and `PSBBN done: NAME = RESULT`.
Original function is called once and return is unchanged; MODULE builds keep
upstream behavior. Native text-console entry logs before takeover, and stopping
the early renderer now leaves yellow K6 NATIVE GS CONSOLE TAKEOVER. This should
distinguish a specific initcall from the point where early display output ends;
it is diagnostic, not a claimed fix of the still-unknown later boot failure.

Full kernel and launcher builds, kernel ELF checks, mode-gate mutation checks
and binary audit passed. Audit parses every initcall slot and verifies its
named wrapper target. Host test checks exactly-one invocation and propagation
of a negative return in addition to existing early-console buffer/stop checks.
Kernel SHA256 `3bbcc80676050594aee28b8dc19ea5b75a3f3be9985211c42314bdf7904a03ae`.
ELF: 4,635,612 bytes, SHA256
`b249126b9b425f034bc1d2b7d794b354c01cdd00a38042f416b9569f6ff599b8`.
Artifacts: psbbn-mmce-boot-v0.6.7.zip and corresponding -source.zip.
User replaces USB psbbn_boot.elf themselves; no PC image/card edits made.
Next evidence: final numbered lines with init/done names or K6 marker, plus
PSBBN-BOOT-v067.log. No v0.6.7 hardware result, PSBBN boot, commit/push/upload.

## Latest: v0.6.5 reaches K5; v0.6.6 adds early Linux log

User reports yellow `K5 LINUX START_KERNEL`, unchanged after about a minute.
No photo/log for this run was supplied. This demonstrates kernel entry and
CPU/PROM/MMU initialization, reaching the checkpoint just before start_kernel;
it does not localize a subsequent stop to a specific operation. The user was
told one minute is a troubleshooting cutoff, not a measured normal boot time.
The native GS console starts later, so printk messages can remain invisible
behind the last marker. Successful full Linux/PSBBN boot remains unconfirmed.

v0.6.6 retains the assembly privilege fix and registers an early printk console
before K5. It uses the hardware-validated framebuffer path, shows 18 numbered
lines that wrap plus partial lines, and adds initialization progress messages
for architecture/traps/IRQ/scheduler/timers/console/memory and PS2 DMA/SIF/RTC/
power. `ps2con_startup` silences the early callback before changing GS ownership.
It does not unregister/acquire console_sem there (avoiding recursive locking);
callback/state are persistent rather than freed init data. Rendering errors
silence the early output instead of indefinite retries. This is a visibility
build for a still-unknown post-K5 issue, not a claimed fix of that stall.

Full kernel/launcher compilation, binary audit, mode-gate mutation checks and
kernel ELF bounds tests passed. A host test compiles actual boot_trace.c and
covers partial/split text, CR, wrapping/bounds, repeat init, stop and rendering
failure. No claim that host tests validate actual Linux/GS takeover.
Kernel SHA256 `55df1e4f32e861299f1526275aa75791501ef94638f7034de1894febab1e2494`.
ELF: 4,620,380 bytes, SHA256
`557bd6f2eed020947e45d29bed6f0c936570f24454d57eedeb23cc1e958f860f`.
Artifacts: psbbn-mmce-boot-v0.6.6.zip and corresponding -source.zip.
User copies USB psbbn_boot.elf themselves; SD image/profile unchanged. Next
evidence: photo of all numbered early Linux log lines and PSBBN-BOOT-v066.log
if stalled. No v0.6.6 console result, successful PSBBN boot, commit/push/upload.

## Latest: confirmed privilege-ordering bug fixed in v0.6.5

The v0.6.4 photo shows the yellow T1 screen test, T2 transfer return, H0, H1
and H2, but no H3. Thus early output works and the stop is inside enter_linux
before its first marker. Saved photo: build/hardware-v064-h2-screen.png.

**Confirmed code bug:** v0.6.4 disassembly has LW from the KSEG0 kernel ELF
header at 0x01002684 (`phoff`) and 0x01002690 (`entry`, DIntr delay slot), before
the kernel-mode Status write at 0x010026a0. The C prologue dereferences protected
pointers while still in user mode. This is consistent with the observed H2
stop and provides concrete evidence beyond the earlier cache-call guesses.
Disassembly retained as ignored build/v064-disassembly.txt.

v0.6.5 fixes this by entering through a seven-instruction assembly gate,
psbbn_enter_linux: DI, sync.p, LUI/CP0 Status=0x30010000, sync.p, direct tail jump
to the separate noinline psbbn_enter_linux_privileged C body, NOP delay slot.
There is no load/store/call before the mode change, and a0 preserves iopaddr.
The C body keeps the existing direct cache routine, KSEG transition, compact
screen, fault handlers and markers. Sound remains disabled. Kernel unchanged
from v0.6.4 (SHA256 913c2c2238c86a3d8e9db170e058d8966aaa6417ddd54dafee8b969953c2434d).

Launcher compilation and binary audit passed. New tests parse the actual ELF
gate, enforce mode-before-C ordering and the caller, reject early-load,
wrong/missing Status and bad-jump mutations, and reject the v0.6.4 ELF.
The standard boot build now runs the gate tests. No hardware-boot success is
claimed yet; this fixes the evidenced code defect, not every later boot stage.
ELF: 4,615,132 bytes, SHA256
`e8d6661618758bd8008dc91ab98b1dc1c8f3e1e2b0b3926c406b7dc3fa5fdec2`.
Artifacts: psbbn-mmce-boot-v0.6.5.zip and corresponding -source.zip.
User copies only USB psbbn_boot.elf; SD setup is unchanged. Next test: photograph
the final handoff/kernel stage and return PSBBN-BOOT-v065.log if it stalls.
No v0.6.5 hardware result, successful Linux/PSBBN boot, commit, push or upload.

## Latest: v0.6.3 photo exposes offscreen diagnostics; v0.6.4

The user tested v0.6.3 and supplied a photo showing that version with H0/H1
at the bottom, unchanged visibly. Therefore removing the SDK cache syscalls
did not resolve the visible issue. Inspection found a definite diagnostic bug:
SDK scr_printf permits 40 rows, but init_scr's scissor is only 640x224 (28 rows).
The startup page filled all 28 rows at H1; H2 was offscreen. Earlier claims
that the H1-to-H2 cache calls localized the hang are unreliable. Do not repeat
that as a proven root cause. Photo saved to ignored build/hardware-v063-h1-screen.png.
The user is frustrated with repeated test builds and asked to fix the issue.

v0.6.4 clears the handoff page before H0 and adds a T0/T1/T2 early screen
transfer check while the old EE kernel is still intact. The persistent yellow
marker is at scanline 160, away from bottom overscan. The early renderer now
uses bounded normal GIF DMA, following the working SDK display path, rather
than unvalidated CPU stores to the GIF FIFO. A two-quadword packet has a
private aligned 64-byte cache line, written only through its uncached alias.
This is rebuilt into both launcher and kernel. Sound remains disabled;
post-shutdown SDK cache calls remain omitted. The underlying Linux handoff
failure is still not demonstrated fixed; actual console output is required.

Full kernel and launcher builds, renderer host bounds/failure checks, kernel
ELF validation and boot audit passed. Host tests do not validate real GS DMA.
Kernel SHA256 `913c2c2238c86a3d8e9db170e058d8966aaa6417ddd54dafee8b969953c2434d`.
ELF: 4,615,376 bytes, SHA256
`f162553eec2e01eefa6f1e4599e18108cc9271deb35e2cbc9aab7dc9d77d9b19`.
Artifacts: `psbbn-mmce-boot-v0.6.4.zip` and corresponding `-source.zip`.
User copies only USB psbbn_boot.elf themselves. SD card/image unchanged.
Next evidence: photo of the new compact HANDOFF page (T0/T1/T2, H0-H7, K0-K5 or
fault values) and PSBBN-BOOT-v064.log. No successful Linux/PSBBN boot or v0.6.4
hardware result, commit, push or public upload exists.

## Latest: v0.6.2 stops at H1; v0.6.3 removes SDK cache syscalls

The user supplied `PSBBN-BOOT-v062.log` (saved as ignored
`build/hardware-v062.log`) and reported seeing H0 and `H1 EE services closed`,
but not H2. All 11 preflight groups passed again. The next source operations
after H1 were SDK `FlushCache(0)` and `FlushCache(2)`, before entering the
privileged handoff. This narrows the suspect interval; the exact syscall and
internal cause have not been proven.

v0.6.3 removes those redundant SDK cache-flush calls. `enter_linux` still
disables interrupts, enters kernel mode and invokes the direct D/I-cache
routine before changing aliases or overwriting the old kernel. All H/K and
exception diagnostics remain; sound stays disabled. The instrumented v0.6.2
kernel is reused unchanged. No SD card/image change is needed.

Launcher build and boot audit passed. Audit also checks the launcher object
does not reference SDK FlushCache and retains the direct cache routine.
ELF: 4,613,720 bytes, SHA256
`130564bba2250c7ff2911092ec11b5e5fa92f2d6e7d583c097e071f1cc3ef9ea`.
Artifacts: `psbbn-mmce-boot-v0.6.3.zip` and corresponding `-source.zip`.
The user copies `psbbn_boot.elf` to USB themselves. Next test: report the final
H/K stage or exception values and `PSBBN-BOOT-v063.log`. There is no v0.6.3
hardware result or confirmed Linux/PSBBN boot. No commit/push/upload occurred.

## Latest: v0.6.1 reaches handoff; v0.6.2 diagnostic build

The user tested v0.6.1. The new log again passed all 11 groups and writable
reopen. The photo additionally proves all final IOP modules loaded, writable
block-service capacity plus all 66 bridge samples passed, and intrelay loaded.
Last visible lines: `Bridge samples passed. Starting Linux at 80010490` and
`USB checkpoint saved. Photograph any Linux error shown next.` No actual Linux
entry or PSBBN boot is confirmed. The USB log ends before reset by design.
Hardware evidence is saved under ignored `build/hardware-v061.log` and
`build/hardware-v061-handoff-hang.png`.

v0.6.2 keeps sound disabled, removes `romcons` from the boot command line,
flushes the changed stack alias, adds visible handoff markers H0-H7, and adds
early kernel markers K0-K5 (entry after BSS clear, CPU probe, PROM and
start_kernel). A register-only renderer uses the existing debug framebuffer;
there are no BIOS, DMA or heap dependencies after kernel replacement.
Temporary exception vectors now show CAUSE/EPC/BADVADDR/STATUS using a separate
4 KiB emergency stack below the SDK stack. The cause of the v0.6.1 stall is
still unknown; these are diagnostic changes, not a demonstrated boot fix.

Full kernel and combined launcher builds, boot audit, kernel ELF validation
and renderer host checks passed. The renderer test covers clipping, bounded
strings, send failures and hex formatting; it does not validate actual GS output.
Kernel SHA256: `f2e5a659987d5af94a56c4330a5a1d4b29d7091921d968b48fe400f24c80da88`.
ELF: 4,613,792 bytes, SHA256
`e47eb5b4d82f1f7eb3926f57f5d8327754c4f28a212d08a156a90c213512145f`.
Artifacts: `psbbn-mmce-boot-v0.6.2.zip` and corresponding `-source.zip`.
Next test replaces only USB `psbbn_boot.elf`; the user prefers to copy it
themselves. Photograph the final marker/exception or Linux screen and return
`PSBBN-BOOT-v062.log`. No v0.6.2 console result, commit, push or upload exists.

## Hardware result and v0.6.1 recovery

The user supplied a full v0.6 USB log and final-screen photo. **All 11 real-image
preflight groups passed on the console**, including all 66 reads via each of
FILEXIO/FHI/ATA, >4 GiB offsets, APA discovery, read-only PFS and complete boot
file checks. Persistent handle cleanup passed; reopening read/write succeeded.
After final IOP reset, sio2man, linux_mcman, linux_mcserv, linux_padman and libsd
returned successfully. The screen stopped after `Module libsd: id=29 start=0`;
the next synchronous load is SDRDRV. Its internal cause is unknown. No Linux
entry or PSBBN boot was demonstrated.

v0.6.1 is a sound-disabled recovery test: omits libsd/SDRDRV from the boot ELF,
disables Linux `CONFIG_PS2_SD` and `CONFIG_SOUND`, and prints each module name
before loading it, with additional bridge progress. This is a workaround to
reach the next boot stage, not a demonstrated sound-driver fix. New log:
`PSBBN-BOOT-v061.log`. Existing card image/profile need no change after this
attempt, which never reached Linux. Replace only USB `psbbn_boot.elf`.

Card setup was corrected on E: (16 GB exFAT, disk capacity 15,931,539,456 bytes):
renamed `PSBBN/psbbn-v4.3-eng-7g.img` to `PSBBN/psbbn.img`; exact 7 GiB size,
matching original fingerprint file and all 66 host sample checks passed.
The earlier 16 GB card (16,231,956,480 bytes) rejected format, partition rebuild
and direct writes with bad-block/CRC errors and was replaced. Reader serial
121220160204 is shared between inserted cards, not a unique card identifier.

The v0.6 implementation notes below are historical; current boot source uses
the sound-disabled recovery path. Full kernel and launcher builds, boot audit,
and the kernel ELF bounds/corruption tests passed. v0.6.1 ELF: 4,600,704 bytes,
SHA256 `c40668fc8928bee97eba4a1270a68ce18a37c10ba7b6258f0693796848e4e14f`.
Kernel SHA256 `f2274a83a53994f433a1993e550b9631ad38547a9f3a54229d362fa49debfa9d`.
Artifacts are `psbbn-mmce-boot-v0.6.1.zip` and its `-source.zip` companion.
The user chose to copy the ELF themselves; no USB file was changed. Original
v0.6 ZIPs are retained. No commit/push/upload or v0.6.1 console result exists.
The supplied screen photo is retained under ignored build/hardware-v06-sdrdrv-hang.png.

Resumed at the user's request. **v0.6 now has a combined preflight and Linux
boot-attempt ELF**, `psbbn_boot.elf`. It compiles and passes host checks.
There is no console boot-attempt result and no demonstrated successful PSBBN boot.

## Latest work: kernel bridge and boot integration

- Published kernel pinned at `80b14105512ed6114c1defa8e9dee40679072874`.
  `kernel/psbbn_mmce.c` replaces physical IDE at major 3 (/dev/hdaN), retains
  existing APA extent mapping, uses a worker and serialized SIF block RPC.
- `iop/block.c` uses Neutrino ATAD; checks protocol, lengths, bounds and errors.
  Writes require `--write`. Read-only and checked read/write ATAD variants are
  separate; FHI seek errors are now checked before transfers.
- Complete Linux kernel built with the pinned historical GCC 2.95.2 archives
  in a Linux container. Slim RPC IRQ patch applied; physical IDE absent.
- Combined `psbbn_boot.elf` runs all v0.5 groups, requires a USB checkpoint,
  opens the image read/write, preserves its MMCE handle through final reset,
  loads the Linux services, checks all 66 samples through the new bridge, then
  hands off to the embedded source-built kernel with TGE SBIOS.
- Loader occupies RAM starting at 0x01000000; its reserved stack is below the
  bootpage at 0x01fff000. ELF validation protects SBIOS/loader/stack memory.
  Handoff switches to KSEG0 before old user mappings are removed.
- TGE and intrelay pinned at kernelreloaded/Pcsx2-lnx
  `eb5a2a23dc9894322aea571242365460241eb9f6`. No upstream UI/fake RAM defaults.
- Static audit found SDK SDRDRV imports libsd 1.5 but SDK libsd exports 1.4.
  Added source-built compatible SDRDRV: libsd 1.4 imports, legacy callback/EECB
  compatibility, unsupported return for four newer APIs unused by TGE.
- Complete kernel/ELF builds pass. Host tests cover actual block handler,
  checked/read-only ATA transfers, kernel ELF corruption/memory bounds, profiles
  and synthetic fixture. Boot audit checks internal library versions, Slim IRQ,
  memory ranges, virtual driver presence and no physical DEV9 imports.
- Final lifecycle check closes the persistent read-only ATA0 handle after PFS
  unmount, before the writable reopen. FHI export 7 and diagnostic RPC 4 provide
  this controlled cleanup. A failed close blocks boot; mocked seek/close tests pass.
- Artifacts: `psbbn-mmce-boot-v0.6.zip` and corresponding
  `psbbn-mmce-boot-v0.6-source.zip`. Final ELF SHA256:
  `f7b6caadbdf56715d7772e87b9a50078bfff7bcf6a3b52ebc9c3ea5241fa3509`.
- See `BOOT.md` for exact copy/run instructions and build recipe. The first
  test legitimately attempts a boot; Linux may write to the card image. Keep
  the PC original. No proprietary Sony image/executable was embedded or uploaded.

## Immediate next step

Run the combined hardware test once using `psbbn_boot.elf`, the private 7 GiB
image renamed PSBBN/psbbn.img, and private/real-checks.bin. USB log:
PSBBN-BOOT-v06.log. It stops before final IOP reset; photograph later module,
Linux or PSBBN screens. No sound/network/PSBBN runtime compatibility is claimed.
No commits, pushes or public uploads were made. All earlier implementation
gate notes below are historical; the latest state is described above.

## Work completed November 4

- Added `src/real_image.c`: reads `PSBBN/psbbn.img` and a matching
  `PSBBN/real-checks.bin`; checks exact 7 GiB size, 66 host-fingerprinted reads
  (including 2/4 GiB crossings and tail), FHI/ATA reads after IOP reset,
  APA discovery, read-only PFS `__system`, and complete boot-file CRCs.
- Added pinned SDK APA/PFS sources and separate read-only driver builds.
  APA has no DEV9 import. ATAD rejects writes and propagates FHI errors.
  APA/PFS reject dirty journals without replay/reset; PFS rejects expansion.
  The v0.4 ATAD transfer path is preserved.
- Added `tests/make_real_checks.py`, sparse-image/profile corruption tests,
  and host tests of the actual read-only ATAD function with mocked FHI.
  Complete pinned-container build and focused host tests passed. Existing
  synthetic-disk validation still passes. CI was updated but not run remotely.
- Local ignored `private/real-checks.bin` is 840 bytes (66 samples), generated
  from the 7 GiB image and archive boot-file exports. `private/osdboot.elf`
  and `private/vmlinux` are private analysis copies, never public artifacts.
  Image APA and `__system` PFS journals were checked clean without writing.
- `REAL-IMAGE.md` contains exact copy/run instructions. Distribution includes
  both `psbbn_suite.elf` and `psbbn_real.elf`; no private image or profile.
  Local package: `psbbn-mmce-preflight-v0.5.zip`; ELF SHA256:
  `e1536f39f876b6b04721b302319bd050e1b6203e0e5e83127c2b4c81725c917d`.
- **Critical boot finding:** the shipped kernel's `ps2_ide_select` performs
  direct IDE register accesses; `ps2_ide_setup_dma` binds SIF RPC `0x2000` and
  `0x2001`. ATAD interception alone is insufficient. Evidence and next work:
  `BOOT-INVESTIGATION.md`. Neutrino resident hooks were reviewed, not integrated.
  Kernel PT_LOAD aliases RAM `0x00010000` through `0x00228f50`, overlapping
  Neutrino's default resident memory. Credited source is
  https://github.com/CosmicScale/PSBBN-Definitive-Patch-Kernel.

## Previous next step (superseded above)

Investigate a kernel-side storage redirect and the Linux DMA RPC contract,
then integrate loader reset residency and memory layout. Do not add a plain
jump to osdboot or claim Neutrino EE hooks provide Linux storage. The final
single-ELF PSBBN boot objective remains unfinished. Keep hardware tests bundled
to avoid unnecessary card swaps. No commits or uploads were made this session.

The previous handoff's baseline and detailed image records follow.

## Goal and constraints

Boot the original PSBBN interface on a PS2 Slim through MMCE, ultimately from
one normal homebrew ELF. Reuse Neutrino's ATA emulator. Keep work on `probe`
under `experiments/psbbn-mmce`. Never commit or publish Sony binaries/images.
The user wants comprehensive tests in one run to minimize unplugging devices.
Their intended test card is **8 GB, formatted exFAT**. Exact console model and
memory-card device/firmware have not been supplied.

## Confirmed hardware milestone

The user photographed **all 11 v0.4 test groups passing**:
MMCE file reads, an exact 8 MiB synthetic disk scan, mixed/repeated reads,
file-handle continuity across an IOP reboot, FHI capacity/data, virtual ATA
capacity/data, request bounds checks, and MMCE reinitialization after ATA.
Displayed verified rates: file 1220 KiB/s, FHI 1060 KiB/s, ATA 1060 KiB/s.
These include validation/RPC overhead. The screen reports a USB log at
`mass0:/PSBBN-DIAG-v04.log`; its contents have not been provided/read.

This proves the tested **MMCE -> FHI -> Neutrino ATAD read path**, not APA/PFS
on the PS2, offsets above 4 GiB, writes, or PSBBN boot. The v0.4 ELF only tests
`PSBBN/diagnostic.img` (8 MiB); it cannot launch the real image below.

## Real test image prepared locally

File: `private/psbbn-v4.3-eng-7g.img`

- Exactly 7,516,192,768 bytes: **7 GiB / about 7.52 decimal GB**.
- SHA256: `f272b27b35bf69a9da1b747fb14746474f1a27659b5474940c6a57a506498903`.
- Rebuilt from the creator's PSBBN 4.3.0 archive + English pack 2.0.1, both
  checked against their published archive SHA1 values. Not a truncated image.
- APA checksums/bounds, EXT2 fsck, critical EXT2 file samples, all PFS regular
  file exports, and full mapped filesystem-payload readbacks passed on the PC.
- Internal music filesystem is FAT32 with `MusicCh/contents` created.
- Separate optional PS2 Linux (`__linux.3`) omitted. See SMALL-IMAGE.md for layout.
- **Not boot-tested.** No MBR boot payload installed; intended standalone entry
  is `__system/p2lboot/osdboot.elf` with the published kernel in that directory.

Detailed local records: `private/verification-7g.json`,
`private/COMPACT-IMAGE-INFO.txt`, `private/compact-layout.json` and filesystem logs.
Private prototype builders: `private/build_7g.py`, `private/populate_7g.py`.
They use WSL Ubuntu, image-only loop devices and temporary Linux build folders.
The temporary `/tmp` contents may not survive a later WSL invocation.

Unmodified reference retained: `private/psbbn-v2.11.img`, 120,036,786,176 bytes
(111.79 GiB), plus its 578,872,071-byte `.gz` download and verification record.
Do not make more full-size copies unnecessarily. All of `private/` is ignored
by Git and excluded from Actions uploads.

## Next implementation gate

1. Extend the real-image preflight to the 7 GiB image: 64-bit file seeks across
   4 GiB, image-sector comparisons, and APA/PFS discovery through virtual ATAD.
2. Load compatible HDD/APA and PFS drivers, mount `__system`, and verify the
   boot executable/kernel through the PS2 stack. SDK APA defaults import DEV9;
   inspect/build with appropriate dependencies rather than initializing a
   physical ATA controller on Slim. SDK exposes `APA_USE_DEV9` build control.
3. Preserve MMCE/FHI/ATAD across the Sony loader's IOP resets before attempting
   PSBBN. Evaluate Neutrino's existing resident EE core/reset hooks; merely
   jumping out of the diagnostic does not establish this environment.
4. Deliver one launchable test ELF, automatic checks and a USB checkpoint log,
   with exact copy/run instructions. Do not ask the user to test the real image
   using the current v0.4 ELF or claim an attempted/successful PSBBN boot yet.

Latest inspection found the archive's `osdboot.elf` loads at **0x01800000**,
entry **0x018000f0**, memory end **0x018c4b48**. Avoid overlapping that region in
a handoff loader. Its analysis copy is ignored at `private/osdboot-analysis.elf`.
Neutrino's EE core already hooks LoadExecPS2 and IOP reset/reload operations;
inspect `ee/ee_core/src/main.c` and `iopmgr.c` in the pinned upstream checkout.
The PSBBN kernel is Linux 2.4.17 per the project's credits; later Linux storage
and memory assumptions still require investigation.

## Repository / build state

- Repository: https://github.com/dnunezx/PSBBN-Definitive-Project, branch `probe`.
- Latest implementation: `be68919`; hardware record: `3996944`; compact profile:
  `431a6bd`. This handoff adds documentation only.
- Public v0.4 artifact: https://github.com/dnunezx/PSBBN-Definitive-Project/actions/runs/41700692269/artifacts/13262054557
- CI/local build: pinned SDK Dockerfile + `build.sh`; suite C validation tests
  and byte-for-byte generated-disk checks pass. Relevant sources are vendored;
  exact Neutrino/MMCE revisions and adaptations are in VENDOR.md.
- No new boot-launcher source was implemented after the compact-image work.
- No agent-started download, image build, or hardware test remains running.
