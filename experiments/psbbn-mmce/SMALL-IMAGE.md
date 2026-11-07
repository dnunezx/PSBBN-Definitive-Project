# 7 GiB PSBBN image

PSBBN 4.3.0 with English language pack 2.0.1, prepared for an exFAT MMCE card.
Use with the matching `real-checks.bin` and `psbbn_boot.elf`; see [BOOT.md](BOOT.md).

- Image filename on the card: `PSBBN/psbbn.img`
- Size: 7,516,192,768 bytes (7 GiB)
- SHA256: `f272b27b35bf69a9da1b747fb14746474f1a27659b5474940c6a57a506498903`
- Minimum free space: the full image size plus the checks file

The image has APA/PFS system partitions, EXT2 root/navigation/data/application
partitions, swap, and a FAT32 music partition. The optional separate PS2 Linux
installation is omitted. Media/download space is intended for testing.

APA checksums, EXT2 read-only filesystem checks, PFS file comparisons and
filesystem payload readbacks passed during preparation. The v0.6.12 launcher
has a reported physical PS2 boot. Sound is disabled; controller input,
networking and repeat-boot reliability remain unconfirmed.

Normal use changes image contents, so this SHA256 identifies the clean download,
not a card image after PSBBN has run. Keep a clean backup.
