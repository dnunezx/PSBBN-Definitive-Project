# Real-image preflight v0.5

`psbbn_real.elf` tests the private 7 GiB PSBBN image. It does not execute
`osdboot.elf` or attempt PSBBN boot. v0.4 remains available separately as
`psbbn_suite.elf`. The two programs use different image paths.

## Files and run instructions

Prepare an otherwise-empty 8 GB exFAT MMCE card with:

```text
PSBBN/psbbn.img        private psbbn-v4.3-eng-7g.img, renamed
PSBBN/real-checks.bin  matching host-generated fingerprint profile
```

The local matching profile is `private/real-checks.bin`. Do not copy the old
120 GB image or rename `diagnostic.img` to `psbbn.img`. No Sony files are
included in public artifacts. To recreate fingerprints from an image and
verified exports of its two boot files:

```sh
python3 tests/make_real_checks.py IMAGE OSDBOOT VMLINUX real-checks.bin
```

Copy `psbbn_real.elf` to a USB drive, leave that drive connected for logging,
and launch it from your usual ELF launcher without arguments. Either MMCE slot
is searched. It needs no external IRX files or `probe.dat`. Power off when the
finished summary appears. Do not interrupt while a USB checkpoint is saving.
The program overwrites `mass0:/PSBBN-REAL-v05.log` (or mass1/mass fallback).
If a driver hangs, the last saved checkpoint identifies the stage reached.
Keep the v0.4 log separately if wanted.

## Automatic checks

1. Profile integrity, exact image size, guarded FILEXIO reads with 64-bit seeks.
2. Samples crossing 2 GiB and 4 GiB, the disk tail, APA headers, filesystem
   metadata, and repeatable mixed offsets. The current local profile has 66 samples.
3. Persistent MMCE handle across reset, FHI/ATA capacity and the same sample
   CRCs through both interfaces.
4. APA directory discovery, including `__system`, `__linux.1` and `__linux.9`.
5. Read-only mount of `__system`, complete CRC/length checks of
   `p2lboot/osdboot.elf` and `p2lboot/vmlinux`, and ELF32 little-endian MIPS headers.
6. Restore MMCE access and save the full summary to USB.

The image handle is opened read-only. A separate Neutrino ATAD build rejects
sector writes before calling FHI, validates ranges and returns read errors.
The v0.4 ATAD build remains unchanged. APA is built without `APA_USE_DEV9`;
no physical ATA/DEV9 driver is loaded. APA/PFS journal replay/reset is disabled
for these builds; invalid/dirty journals cause a failure instead of repair.
PFS superblock auto-expansion is rejected.

Sample CRCs are read-error checks, not a full-image hash or authentication.
They do not validate every image byte. Module loading, filesystem RPCs and IOP
reset can still hang; diagnostic FHI/ATA RPC calls retain the 10-second timeout.
No speed or writes test is included in this preflight.

## Validation and remaining boot work

The program builds locally with the pinned PS2SDK container. Host tests exercise
profile corruption/truncation/ranges, sparse 7 GiB reads across 4 GiB, and the
actual read-only ATAD transfer function with mocked FHI failures/writes.
**No v0.5 hardware result has been recorded.**

The next boot stage must retain/reload MMCE/FHI/ATAD through the loader's IOP
resets, verify resident memory placement, and establish how the Linux kernel
reaches storage. Neutrino's EE hooks are a candidate for the loader phase;
they are not yet integrated. A plain jump from this preflight is insufficient.
This intermediate build does not yet fulfill the final single-ELF PSBBN boot goal.
