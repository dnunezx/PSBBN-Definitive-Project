# Paused handoff — November 3, 2026

**Paused at the user's request. Resume only when the user returns.**
The next session should implement the real-image test / boot launcher. There
is no working PSBBN boot ELF yet, and no additional card swap is needed now.

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
