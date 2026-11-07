# Automatic diagnostic suite v0.4

Checks MMCE, FHI and Neutrino ATA reads in one run using a generated 8 MiB disk.

## Copy once, run once

1. Copy the download's entire `PSBBN` folder to the root of the MMCE microSD,
   including `probe.dat` and the new 8 MiB `diagnostic.img`.
2. Copy **`psbbn_suite.elf`** to USB and launch it with no arguments. Leave both
   the USB drive and memory-card device connected for the whole run.
3. Let all three stages finish. Photograph the final PASS/FAIL/SKIPPED summary.
   If USB logging works, `PSBBN-DIAG-v04.log` is saved at the USB root. Read that
   file on your PC after powering off. A checkpoint is saved before virtual ATA
   starts, so a later hang may still leave a useful partial report.

No external Neutrino installation, command line, or PSBBN image is required.
The test disk contains generated diagnostic bytes, not APA partitions or Sony
files. The ELF embeds the matched MMCE FHI and Neutrino ATAD modules.

## Tests in the same run

- Raw MMCE PING identifiers for both slots and exact 512-byte fixture comparison.
- Test disk size and 36 random/mixed reads of 512 B, 1 KiB, 1.5 KiB, 4 KiB,
  16 KiB and 32 KiB, including first and last sectors.
- Full 8 MiB filesystem scan with every byte checked and buffer guards checked.
- Repeated first/last/first reads to investigate seek and repeatability issues.
- Device-side file-ID extraction and keeping the open image over an IOP reboot.
- Direct FHI capacity, mixed reads, and a full 8 MiB byte comparison.
- Neutrino virtual ATA capacity, mixed reads, and a full 8 MiB byte comparison.
- Rejection of invalid RPC ranges before invoking the storage drivers.
- Restoring the regular MMCE environment and rereading the fixture after ATA.
- Verified transfer rates for the filesystem, FHI, and ATA stages. These include
  data validation and RPC overhead, so they are diagnostic rates, not pure
  hardware throughput benchmarks.

All MMCE files are opened using **FIO_O_RDONLY**. The suite issues no image
writes or formatting commands. The sole file it creates/overwrites is the named
USB report. A driver/module hang can still prevent completion; photograph the
last visible line if this happens. A pending RPC has an approximately 10-second
watchdog; module loading and filesystem calls do not have a universal watchdog.

## What a PASS would establish

A full PASS would demonstrate exact synthetic-image reads through MMCE's
filesystem, the FHI driver, and Neutrino's ATAD emulator across the tested IOP
transition, followed by MMCE reinitialization. It would not establish APA/PFS,
PSBBN boot, write support, offsets above 4 GiB, or game-like concurrent I/O.
The RPC bounds checks exercise the diagnostic's validation, not upstream ATA
error handling. No PSBBN boot is claimed by this suite.

Source pins and small build/lifecycle adaptations are listed in `VENDOR.md`.
The download contains one runnable ELF, `psbbn_suite.elf`. The original v0.3
`ata_probe.elf` remains buildable from source for comparison.
