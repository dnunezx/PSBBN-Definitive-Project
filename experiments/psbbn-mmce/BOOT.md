# PSBBN MMCE boot experiment v0.6.12

This hardware build includes the two SBIOS fixes discovered during emulator
boot testing: configuration reads copy only the requested 15-byte blocks, and
RPC completions with no outstanding packet cannot dereference NULL. The same
kernel and fixed SBIOS reached the English PSBBN network setup screen in our
separate PCSX2 test. The user reports reaching PSBBN on the physical console,
where controller input remains unresolved. The subsequent v0.6.11 photo shows
an MMCE RPC timeout followed by failed root-filesystem reads. v0.6.12 fixes
a confirmed SIO2 hook-selection defect, and the user reports that this version
booted on the physical PS2. Controller input and repeat boots have not yet been
separately confirmed.
Sound remains disabled.

For the next console test, use **psbbn_boot.elf** from this package. Keep the
existing correctly prepared MMCE card's `PSBBN/psbbn.img` and matching
`PSBBN/real-checks.bin`; replace only the ELF on USB. Normal filesystem changes
after a prior boot are supported. The emulator's padded/modified test image and
`psbbn_emulator.elf` are not hardware test files.
Launch from USB with MMCE enabled and USB left connected. Record the final screen
and `PSBBN-BOOT-v0612.log`. Allow any content-management update to complete.

## Shared controller/card interface repair in v0.6.12

The shipped PS2SDK SIO2MAN registers both modern v2.7 and legacy v1.2 export
tables under the same name. The legacy table is first in loadcore's list.
MMCEFHI previously hooked that first match, while the shipped PADMAN and MCMAN
import v2.3. Their calls therefore bypassed the MMCE sharing locks. This is
a confirmed code defect consistent with both controller failure and card-read
timeouts; it does not prove the cause of either observed hardware symptom.

MMCEFHI now selects the highest compatible v2 table, falls back to v1.2 when
no v2 is present, and remembers the exact table for teardown. Host tests compile
the production lookup, hook and relink code: both list orders select v2.7,
modern callers are relinked/restored, and legacy-only operation is retained.
The old first-match selection fails the same test. Kernel, SBIOS and image/profile
contents are not changed by this correction. No filesystem repair was performed.

## Repeat-boot repair in v0.6.11

The reported v0.6.10 log stopped at sample 22, LBA 4202496: current CRC
681d16fa versus original 37005e89. This is the start of the writable
`__linux.9` ext2 filesystem, before Linux is launched. The original PC image's
same sample has CRC 37005e89. A successful boot can legitimately change this
sample, so boot-time checks must not assume immutable filesystem contents.

The boot build now records each sample only after two identical read-only file
reads, then checks every subsequent storage route against that in-memory
reference. It does not rewrite the profile, change the image or repair filesystem
damage. Missing/unstable/mismatched backend samples still stop the boot. Original
profile/size and full boot-file checks remain enforced. The separate standalone
preflight retains strict original CRCs. Controller input is a separate unresolved
physical-hardware issue; v0.6.11 did not include a controller repair.

## Earlier boot investigation

This recovery **boot-attempt** build omits sound. On hardware, v0.6 passed all
11 preflight groups, closed its read-only handle and reopened the image for
writing, then stalled after loading `libsd`, while starting `sdrdrv`. The exact
cause inside that module has not been established. v0.6.1 omits both IOP sound
modules and disables Linux's sound driver to avoid waiting for an absent RPC
service. It also prints each module name before starting it. On hardware,
v0.6.1 loaded every final module, passed all 66 writable-bridge read checks and
loaded intrelay, then left the last screen at "Starting Linux at 80010490".
This does not prove Linux entry was reached.

v0.6.2 adds screen markers H0-H7 during handoff and K0-K5 during early kernel
startup. Before Linux installs its own exception vectors, faults show CAUSE,
EPC, BADVADDR and STATUS instead of silently spinning. The marker renderer uses
the existing debug framebuffer directly, with no BIOS calls or heap.
It flushes stack data after switching virtual aliases and removes `romcons`
from the command line so firmware serial output cannot block that console;
`console=tty0` remains. On hardware, v0.6.2 reached `H1 EE services closed`
but not H2. The next calls were SDK `FlushCache(0)` and `FlushCache(2)`.
v0.6.3 removes these redundant post-shutdown syscalls; the privileged handoff's
direct D/I-cache routine remains in place before alias changes and copying.
The v0.6.3 photo still showed H1. Inspection established that the SDK printer
allows 40 rows but this framebuffer displays only 28: H2 was below the visible
page, so H1 did not reliably identify the stopping operation.

v0.6.4 clears the handoff screen before shutdown and puts the persistent early
marker at row 160, within the visible area. The early renderer now uses bounded
GIF DMA transfers, following the SDK printer's working transfer path, rather
than CPU stores to the GIF FIFO. A T0/T1/T2 screen-transfer check runs before
shutdown while the old kernel remains intact. Its packet occupies a private,
uncached cache line. The kernel includes the same revised renderer.
On hardware, v0.6.4 showed the yellow T1 test, T2 transfer return, and H0/H1/H2,
but never H3. Its compiled `enter_linux` loaded the kernel ELF's `phoff` and
`entry` through KSEG0 before DIntr/the inline kernel-mode switch. Those accesses
occurred while still in user mode. This is a confirmed code-ordering defect
consistent with the observed H2 stop.

v0.6.5 enters through an assembly gate that selects CP0 Status 0x30010000 and
executes sync.p before tail-jumping to a separate non-inlined C handoff body.
There are no loads, stores or function calls before the mode change, so C cannot
hoist protected reads above it. The binary audit checks that exact ordering and
that main calls the gate. Mutation tests reject an early load, wrong/missing
Status write and bad branch; the previous unguarded ELF is rejected too.
On hardware, v0.6.5 reached the yellow `K5 LINUX START_KERNEL` marker and stayed
there for over a minute. This demonstrates entry through the kernel CPU/PROM
and MMU setup paths. It does not identify the later stopping operation: Linux's
native GS console takes over later, leaving intermediate printk output hidden.

v0.6.6 retains the assembly fix and adds an early printk console on the known
working framebuffer. Eighteen numbered log lines wrap on screen; incomplete
lines are displayed too, so a wait before newline remains visible. Progress
messages identify architecture, exceptions, IRQs, timers, native console,
memory and PS2 DMA/SIF/RTC/power initialization. Output becomes silent before
the native GS console takes over; that transition does not acquire console_sem
again. The callback and state remain allocated safely afterwards.
On hardware, v0.6.6 showed working SIF/RTC initialization, networking setup and
the first kernel initcalls. The latest numbered line was "Disabling the Out Of
Memory Killer" from kswapd_init; this is an informational configuration message,
not an OOM failure. Later initcalls can run silently, and the early log turns
off at native GS takeover, so that line alone does not identify a failed call.

v0.6.7 wraps each built-in initcall with `PSBBN init: NAME` and
`PSBBN done: NAME = RESULT`, preserving its slot, call order and return value.
The binary audit verifies every initcall-table slot targets its corresponding
wrapper. Native text-console startup logs its entry and leaves a yellow
`K6 NATIVE GS CONSOLE TAKEOVER` marker before early output is silenced.
The user reports v0.6.7 stops at K6, localizing the visible transition to native
GS text-console startup. The internal reset/mode/DMA operation is not known.

v0.6.8 selects the kernel's existing `graphics` boot option, skipping
take_over_console. Its alternate path uses CRT resolution -1, which records
the current mode without resetting GS or programming CRT hardware. The option
handler now returns 1 so checksetup consumes `graphics` instead of passing it
as an unexpected /sbin/init argument. Driver initialization and MMCE boot
continue normally; this is a workaround for the native text-console transition.
The early log remains active until a userspace program successfully opens the
GS device, then shows `K7 USERSPACE GRAPHICS DEVICE OPENED` and becomes silent
before that program can change the display. The underlying native-console
fault is not claimed fixed. The v0.6.8 run advanced to a kernel panic in
`ps2cdvd_init`; its faulting instruction at 0x801c12f0 stores through an
uninitialized assembler temporary while assigning the block-size table.

v0.6.9 fixes a confirmed legacy-toolchain defect. The old assembler corrupts
address-loading macro expansion on this host; assembling the same CD/DVD source
with the pinned current PS2 binutils preserves the required LUI/index/store
sequences. The complete kernel now uses GCC 2.95.2 code generation with pinned
binutils 2.45.1 for assembly/linking/archive/symbol tools, explicitly O32.
Small assembly-dialect adaptations keep R5900 MMI/FPU operations enabled.
The binary regression symbolically executes all three CDVD table assignments,
checks initialized addresses and values, and rejects the old kernel at the
observed fault. CD/DVD support stays enabled. **The defect is fixed in the
rebuilt binary; PSBBN UI startup still requires console validation.**

## Copy once, then run

On an **8 GB or larger exFAT MMCE card**, place:

```text
PSBBN/psbbn.img         private/psbbn-v4.3-eng-7g.img, renamed
PSBBN/real-checks.bin   private/real-checks.bin, matching that image
```

The filename must be exactly `psbbn.img`, not the PC source image's longer name.
An existing v0.6/v0.6.1 card that passed all checks needs no image/profile changes:
replace only the USB `psbbn_boot.elf` with this recovery build.

The image is exactly 7,516,192,768 bytes. Keep the original PC image.
Neither the image nor its fingerprint profile is included in the boot ZIP.
The old `probe.dat` and `diagnostic.img` are not needed for this program.

On a **USB drive**, place `psbbn_boot.elf`. Leave USB connected and launch that
ELF from your usual launcher with no arguments. The kernel, Linux BIOS and all
extra IOP modules are embedded; no extra kernel or IRX files need copying.

The program automatically:

1. Checks the image size and 66 sampled regions through FILEXIO, FHI and ATAD,
   including reads across 4 GiB; discovers APA; mounts `__system` read-only;
   checks both complete original boot files and restores MMCE.
2. Closes the persistent read-only image handle after unmounting PFS; requires
   all 11 preflight groups, successful handle cleanup and a USB checkpoint.
3. Reopens the image read/write, preserves its MMCE handle through the final
   IOP reset, loads the Linux services, and checks the same 66 samples through
   the new block RPC bridge.
4. Starts a source-built PSBBN Linux kernel with `/dev/hda` supplied by MMCE.
   The original PSBBN installation then follows its normal runlevel-4 startup.

**The Linux stage can write to the card's image**, including normal filesystem,
swap and configuration writes. Read-only preflight checks happen first. After
a boot attempt, normal filesystem contents can differ from the original profile.
In v0.6.11 the boot launcher reads each current sample twice through FILEXIO,
then requires FHI, ATAD and the final block bridge to match those live samples.
The profile checksum, image size and complete original boot-file checks remain
enforced. The standalone v0.5 diagnostic still uses strict original sample CRCs.

## Record the result

The USB checkpoint is `PSBBN-BOOT-v0612.log` at the USB root (mass0, mass1 or mass
fallback). It is overwritten on each run. Bring back that log and a photo of
the last screen, including any Linux error or PSBBN interface reached.

The last USB checkpoint is saved **before** the final IOP reset. After that
reset, the screen reports module startup, bridge checking and Linux handoff;
the USB controller is then handed to Linux. Later Linux messages are not
automatically appended to the preflight's USB log. Module/startup operations
can still hang. Bridge calls use a roughly 10-second completion timeout.
Photograph the final screen, including the yellow marker (H3-H7
or K0-K5) or any exception register values; these cannot reach the USB log.
The early renderer assumes init_scr's 640x224 framebuffer and is only used
before the Linux display driver takes ownership. The marker is at scanline 160,
not at the bottom, to avoid overscan and the previous offscreen-text problem.
Once the early Linux log appears, photograph all numbered lines on the final
screen. The four-digit numbers identify the newest lines after the display wraps.

If stopped at a finished preflight/failure screen, power off to exit. If Linux
or PSBBN is running, allow it to finish activity and use its shutdown facility
when available. Keep the PC original for recovery from an interrupted test.

Sound is disabled in this recovery build. Network behavior is not hardware-validated. This build does not
provide Linux networking through the physical DEV9/IDE path. Do not interpret
a storage preflight PASS as a successful PSBBN boot.

## Implementation

`kernel/psbbn_mmce.c` replaces physical IDE at Linux block major 3, retaining
`/dev/hda1` etc. The existing kernel APA mapper handles `__linux.N` partitions
and subpartition extents. A worker thread serializes requests; SIF RPC carries
up to 64 sectors per transfer to `iop/block.c`, which calls Neutrino ATAD.
Checked ATAD propagates MMCE read/write failures. Timed-out Linux RPC buffers
are retained and not reused, so late DMA cannot target freed memory.

The launcher is linked at RAM `0x01000000`; its stack uses `0x01f80000` with
128 KiB reserved, below the bootpage at `0x01fff000`. SBIOS occupies the reserved
area below `0x00010000`; the kernel loader accepts PT_LOAD segments only in
RAM `0x00010000` through `0x00800000`. The loaded kernel and loader regions are
disjoint. Startup switches to KSEG0 before clearing old mappings.

An open-source TGE SBIOS and explicit IOP boot environment replace the Sony
loader. Neutrino's resident EE core is not required in this path. The Linux
kernel includes the Slim RPC interrupt patch. `CONFIG_PS2_SD` and `CONFIG_SOUND`
are disabled, and neither `libsd` nor `sdrdrv` is linked into the launcher.
The v0.6 sound adaptation remains in source for later investigation. No physical ATA/DEV9 driver
is initialized, and no proprietary Sony executable/image is embedded.

The ATA flush export is a stub in Neutrino; the bridge explicitly rejects a
durable-flush RPC rather than advertising a guarantee it cannot provide.

## Build and source

Kernel source: CosmicScale/PSBBN-Definitive-Patch-Kernel commit
`80b14105512ed6114c1defa8e9dee40679072874` (GPL 2.0). Boot BIOS/relay source:
Florin9doi/Pcsx2-lnx commit `eb5a2a23dc9894322aea571242365460241eb9f6`
(TGE AFL 2.0; project GPL 2.0). SDK sound adaptation uses the pinned source
revision documented in `VENDOR.md`. The boot package includes license texts;
the companion source ZIP supplies the exact kernel source archive and local
source changes/build scripts. No Sony image is supplied.

From this experiment directory in PowerShell:

```powershell
.\kernel\prepare.ps1
docker build -t psbbn-linux-build -f kernel/Dockerfile kernel
docker run --rm -v "${PWD}:/work" psbbn-linux-build bash kernel/build.sh
docker build -t psbbn-probe-build .
docker run --rm -v "${PWD}:/work" psbbn-probe-build bash build-boot.sh
```

The old compiler archives are downloaded into ignored `private/` and checked
against pinned SHA256 values. Kernel compilation runs on the container's
case-sensitive filesystem, avoiding Windows filename collisions. The legacy
compiler emits assembly; pinned current PS2 binutils assemble and link it.
`as-wrapper.sh` translates the old CPU flags and explicitly selects O32;
`modern-binutils.patch` updates R5900-specific ISA directive spellings. It writes
only generated build outputs, never the 7 GiB image. The ordinary `build.sh`
continues to package the standalone read-only preflight; the boot artifact
uses `build-boot.sh` and `boot-dist/`.

Validation: full kernel and combined ELF builds; actual bridge handler tests;
checked/read-only ATAD tests; kernel ELF bounds/corruption tests; module library
version and memory-layout audit; profile corruption and 8 MiB fixture checks.
These establish host/build behavior, not PS2 execution or a successful boot.
