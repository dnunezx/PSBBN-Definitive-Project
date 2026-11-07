# Read-only image preflight

`psbbn_real.elf` validates the 7 GiB PSBBN image without booting Linux or
writing to it. For PSBBN startup, use [psbbn_boot.elf](BOOT.md) instead.

Place `PSBBN/psbbn.img` and its matching `PSBBN/real-checks.bin` on the
MemCard PRO2's exFAT microSD. Copy `psbbn_real.elf` to USB and launch it with
no arguments, leaving both devices connected. Either MMCE slot is searched.
No external IRX files are needed.

The preflight checks profile integrity, image size, sampled reads across 4 GiB,
FILEXIO/FHI/ATA agreement, handle continuity across IOP reset, APA discovery,
read-only PFS access and complete boot-file fingerprints. APA/PFS journal replay
and reset are disabled. A passed preflight establishes storage reads, not a
successful PSBBN boot.

The program saves `PSBBN-REAL-v05.log` to USB. If it hangs, photograph the last
screen. Power off after the finished summary.

To generate a matching checks file from an image and verified exports of
`__system/p2lboot/osdboot.elf` and `__system/p2lboot/vmlinux`:

```sh
python3 tests/make_real_checks.py IMAGE OSDBOOT VMLINUX real-checks.bin
```

Build with `bash build.sh` inside the pinned SDK container. The output is in
`dist/`, together with the automatic diagnostic suite and generated fixtures.
