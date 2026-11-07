# PSBBN MMCE boot v0.6.12

Boot PSBBN on a PS2 Slim from a MemCard PRO2. The launcher uses Neutrino ATA
emulation and MMCE FHI, with a custom Linux storage bridge and TGE SBIOS.
A physical PS2 boot has been reported. Controller input and repeat-boot
reliability remain unconfirmed. Sound is disabled; networking is unverified.

## Setup

1. Enable MMCE on the MemCard PRO2 with compatible firmware.
2. On its exFAT microSD card, place `PSBBN/psbbn.img` and the matching
   `PSBBN/real-checks.bin`. The image is exactly 7,516,192,768 bytes. An 8 GB
   or larger card needs at least this much free space plus the small checks file.
3. Place `psbbn_boot.elf` on a USB drive and launch it with no arguments from
   your usual ELF launcher. Leave USB and the MemCard PRO2 connected.
4. Allow startup and any content-management update to finish.

Copy the image as a file; do not flash it over the microSD's partition table.
The image exceeds FAT32's per-file limit, so the MMCE card must use exFAT.
The launcher contains the kernel, SBIOS and extra IOP modules.

For an existing matching image/checks pair, replacing the USB ELF is sufficient.
The launcher-only ZIP does not contain the disk image; a full-image package
contains the `PSBBN` folder as well.

## Checks and operation

Before Linux starts, the launcher validates image/profile bounds, repeated
sample reads through FILEXIO/FHI/ATAD, APA/PFS discovery and the original boot
files. It then reopens the image for writes and checks the final RPC bridge.
Filesystem samples are compared against repeated live reads, allowing normal
changes from earlier boots. The checks file itself is not rewritten.

Linux and PSBBN can write to the image, including configuration, swap and
filesystem metadata. Keep a clean backup and use PSBBN's shutdown facility when
available. The bridge does not provide a durable-flush guarantee.

If startup fails, save `PSBBN-BOOT-v0612.log` from the USB root and photograph
the final screen. The USB log ends before the final IOP reset; later Linux
messages appear only on screen. Include console model, controller type/port,
MemCard PRO2 firmware and the step that failed when reporting a result.

## Implementation

The launcher is linked at physical `0x01000000`; its kernel loader accepts
segments only in `0x00010000` through `0x00800000`. An assembly gate enters
kernel mode before protected memory accesses. TGE SBIOS occupies the reserved
area below `0x00010000`.

`kernel/psbbn_mmce.c` supplies Linux block major 3 and `/dev/hdaN`, retaining
the kernel's APA mapping. A worker serializes requests of up to 64 sectors over
SIF RPC to `iop/block.c`, which calls checked Neutrino ATAD and MMCE FHI.
Physical IDE/DEV9 storage is disabled. Timed-out RPC buffers are retained rather
than reused while DMA could still be pending.

SIO2MAN lookup selects the modern export table used by PADMAN/MCMAN, ensuring
controller and card transfers use the same sharing locks. SBIOS configuration
reads are bounded to the requested blocks, and duplicate RPC completions with
no pending packet are ignored. Source revisions and licenses are in [VENDOR.md](VENDOR.md).

## Build

From this directory in PowerShell, with Docker installed:

```powershell
.\kernel\prepare.ps1
docker build -t psbbn-linux-build -f kernel/Dockerfile kernel
docker run --rm -v "${PWD}:/work" psbbn-linux-build bash kernel/build.sh
docker build -t psbbn-probe-build .
docker run --rm -v "${PWD}:/work" psbbn-probe-build bash build-boot.sh
```

The preparation script fetches pinned kernel source and SHA256-checked compiler
archives. Compilation runs on the container's case-sensitive filesystem. The
old compiler emits assembly; pinned current PS2 binutils assemble and link it
with O32 ABI. This build does not modify a PSBBN disk image.

`build-boot.sh` audits the resulting ELF, module versions, memory bounds,
privilege gate and CD/DVD table initialization, then packages `boot-dist/`.
`python tests/package_boot.py` creates the launcher and corresponding-source
ZIPs after the build. Full kernel source is included in the source ZIP.
