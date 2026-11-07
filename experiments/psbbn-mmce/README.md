# PSBBN on PS2 Slim / MMCE

Boot PSBBN from a MemCard PRO2 using Neutrino ATA emulation, MMCE FHI and a
Linux block-storage bridge. The launcher embeds the kernel, TGE SBIOS and
required IOP modules; no separate Neutrino installation is needed.

## Hardware boot

Use `psbbn_boot.elf` v0.6.12 with the matching 7 GiB image and checks file.
See [BOOT.md](BOOT.md) for setup and building, and [SMALL-IMAGE.md](SMALL-IMAGE.md)
for image details. A physical PS2 boot has been reported. Controller input and
repeat-boot reliability remain unconfirmed. Sound is disabled.

## Storage diagnostics

- [Automatic suite](SUITE.md): `psbbn_suite.elf` checks an 8 MiB generated disk.
- [Image preflight](REAL-IMAGE.md): `psbbn_real.elf` checks the 7 GiB image
  without booting Linux or writing to the image.

The GitHub Actions workflow builds the diagnostics and runs the regression
checks. Download `psbbn-mmce-diagnostic-suite` from a successful workflow run.
Hardware boot packages are separate from the diagnostic artifact.

## Source

`src/` contains the launcher; `iop/` contains RPC services; `kernel/` contains
the Linux driver and kernel build scripts; `vendor/` contains adapted upstream
drivers and SBIOS. `tests/` contains automated checks and generated fixtures.

[Pinned dependencies and changes](VENDOR.md) and [third-party notices](THIRD-PARTY-NOTICES.md)
identify the source and licenses for included components.
