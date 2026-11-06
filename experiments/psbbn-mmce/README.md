# PSBBN MMCE diagnostic suite v0.4

Latest experiment: [combined preflight and boot attempt v0.6.12](BOOT.md),
`psbbn_boot.elf`. v0.6.12 corrects MMCEFHI's selection of the controller/card
sharing interface; the user reports a successful physical PS2 boot. Controller
input and repeat boots remain unconfirmed. v0.6.11 fixed repeat-boot
preflight checks for writable image content. Keep the existing image/profile
and replace only the USB ELF. The separate PCSX2 test reached English graphical
network setup with the same kernel/SBIOS. See [EMULATOR.md](EMULATOR.md).

The v0.6 hardware run passed every storage preflight check,
then stalled while starting sound. v0.6.1 disabled sound and passed final bridge
checks, then stalled at handoff. v0.6.2 added early screen/fault diagnostics and
reached H1 after EE shutdown; v0.6.3 removes the following redundant cache
syscalls. The v0.6.3 screen still stopped visibly at H1; H2 was offscreen,
making that localization unreliable. v0.6.4 fixes visibility and the early
renderer transfer path. It reached H2. v0.6.5 fixes a confirmed protected-memory
read before kernel-mode entry, using an audited assembly gate. The fixed boot
path reached Linux's K5/start_kernel marker on hardware. v0.6.6 adds visible
early Linux logs and reached driver initialization. v0.6.7 names each initcall
and marks native GS takeover, where the user saw K6. v0.6.8 selects graphical
boot to bypass that takeover and hands the display to the first userspace GS
opener and reached a CD/DVD-init kernel panic. v0.6.9 fixes a confirmed legacy
assembler address-store defect by rebuilding the whole kernel with current
PS2 binutils and explicit O32 ABI. PSBBN UI startup is unconfirmed. Linux can
write to its image.

New development build: [real-image preflight v0.5](REAL-IMAGE.md),
`psbbn_real.elf`. It checks the 7 GiB image, large offsets and APA/PFS boot-file
reads. All these checks passed in the combined v0.6 hardware run. The separate
v0.5 ELF does not attempt PSBBN boot.

The user now uses a 16 GB exFAT test card. A private 7 GiB PSBBN 4.3.0
English working image has been rebuilt and host-checked; see
[SMALL-IMAGE.md](SMALL-IMAGE.md). Its PS2 boot compatibility remains unverified.

**For the new combined automatic tests, read [SUITE.md](SUITE.md) and launch
`psbbn_suite.elf`.** Copy the complete supplied `PSBBN` folder once. The suite
includes the MMCE filesystem, reboot, FHI, virtual ATA and USB report tests.
The current download contains only the combined suite ELF. The instructions
below describe the older single-test `ata_probe.elf`, still buildable from source.

## Legacy v0.3 single-test ELF

Experimental PS2 Slim / MemCard PRO2 diagnostic. **No successful PSBBN boot has
been demonstrated.** This ELF tests storage prerequisites; it does not boot PSBBN.

Replace v0.2 with this version and recopy the supplied fixture before retesting.
v0.2 used newlib `O_RDONLY` (0) with raw FILEXIO instead of IOP `FIO_O_RDONLY`
(1). MMCEMAN packs those flags differently; v0.2 is unsuitable for image tests.
v0.3 fixes the flag and reports a protocol PING for each tested slot. If file
opening fails, it also attempts to open the slot's root directory:

- PING fails: no valid MMCE reply; inspect device/slot/firmware/driver support.
- PING succeeds, root opens, fixture fails: investigate the file path or file.
- PING succeeds, root fails: filesystem access still needs investigation.

These checks narrow the next investigation; they do not prove a cause by themselves.

## First hardware test (no Neutrino or HDD image required)

1. Enable MMCE on your MemCard PRO2 with compatible firmware.
2. Copy the supplied `PSBBN/probe.dat` to `/PSBBN/probe.dat` on its microSD card.
   This file is 512 bytes: values 0 through 255 repeated twice.
3. Copy `ata_probe.elf` to a USB drive and launch it from your usual ELF launcher.
   No arguments or external IRX files are needed. Start from USB for the first
   test so ELF loading and MMCE communication are tested separately.
4. Photograph the results. Expected: successful module loads, 512 bytes read,
   CRC32 `1c613576`, and `Fixture comparison: PASS` on either `mmce0:` or `mmce1:`.
5. Power off to exit. Record console model and firmware in `tests/results.md`.

Default mode resets the IOP and loads embedded SDK IOMANX, FILEXIO, SIO2MAN,
and MMCEMAN modules. It opens the fixture read-only and checks every byte.
A PASS establishes MMCE file access; it does not establish speed, large-image
access, virtual ATA, APA/PFS, or PSBBN compatibility.

To inspect the first sector and reported size of your own image, pass
`--file=mmce0:/PSBBN/psbbn.img` (or `mmce1:`). Compare the displayed CRC32 with
the same bytes on your PC. Arbitrary images are not automatically verified.
Only the first sector is read, even for files larger than 4 GiB.

## Direct ATAD test in an existing Neutrino environment

Pass `--inherit`. This preserves the IOP and loads only our embedded read-only
RPC helper. It imports ATAD library 1.3 exports 4 and 9, matching Neutrino's
`atad_emu`, then requests device 0 information and **one read of LBA 0**.
It needs no HDD/APA, PFS, IOMANX, or FILEXIO. It does not initialize MMCE
or install Neutrino's emulation itself.

Illustrative command using the reviewed upstream syntax:

```text
neutrino.elf -bsd=mmce -ata0=mmce0:/PSBBN/psbbn.img -elf=mass:/ata_probe.elf --b --inherit
```

This command has not been hardware validated. Use your actual paths and a
complete, matching Neutrino module/configuration set. `--b` forwards subsequent
arguments to the target ELF. USB support must be available in the loader
environment for `mass:`. Neutrino's IOP transitions and module-loading hooks
remain part of what this test investigates.

The read RPC has a roughly 10-second completion timeout; service discovery has
a roughly 5-second retry limit. Module loading, IOP reset, and standalone
filesystem operations still depend on drivers and can hang. A module-load
failure can mean missing/incompatible ATAD exports; it does not prove an MMCE
transport failure. A zero ATAD result must be checked against the image's CRC
and bytes: reviewed Neutrino code discards underlying FHI transfer results.

`--hdd` retains the older filesystem-level test against an existing FILEXIO/HDD
stack: status, sector count, and `HDIOC_READSECTOR`. The corrected call retrieves
data from the devctl output buffer. Missing `hdd0:` does not disprove direct
ATAD emulation. Both inherited tests can also reach a physical disk; neither
identifies the backend. Do not combine `--file` with inherited modes.

The diagnostic requests no storage writes, mounts, formats, or repairs.
Neutrino opens virtual ATA images read/write, and other inherited drivers may
write independently. Use a disposable copy of any image.

## Build and download

Run `.github/workflows/psbbn-mmce-probe.yml` on `probe`, then download artifact
**psbbn-mmce-diagnostic-suite** from the successful run. It contains the suite ELF, fixtures,
instructions, licenses, and SHA256 checksums. Build logs are a separate artifact.

The workflow runs checkout/upload actions on Ubuntu 24.04 and compilation inside
a digest-pinned PS2SDK container. Bash, Make, and `file` are explicitly installed.
The compiler is `mips64r5900el-ps2-elf-gcc`.

Local reproduction from this directory:

```sh
docker build -t psbbn-probe-build .
docker run --rm -v "$PWD:/work" psbbn-probe-build bash build.sh
```

On PowerShell, use `${PWD}` in the volume argument if needed. With a configured
PS2SDK and Make, `make` also builds the ELF directly. `build.sh` performs a clean
build, inspects the MIPS ELF header and packages `dist/`. Both processors' source
uses `-Wall -Wextra -Werror`.

See [INVESTIGATION.md](INVESTIGATION.md) for pinned source references. Establish
the MMCE fixture PASS, then compare direct ATAD reads with the image. Only then
investigate APA/PFS and PSBBN boot requirements. No second ATA emulator is added.
