# Experimental 8 GB card profile

The user intends to test on an **8 GB exFAT SD card**. A new local working
image has been built at **7 GiB / 7,516,192,768 bytes** (about 7.52 decimal GB).
The card must have at least that much free space, plus room for the launcher.
This is an experimental capacity target; PSBBN boot at this size is unverified.

The image was rebuilt from the creator's PSBBN 4.3.0 archive and English
language pack 2.0.1. It is not a truncation of the 120 GB v2.11 master. All Sony
files and images stay under ignored `private/`; no image is in Git or Actions
artifacts. The unmodified v2.11 master remains available for comparison.

## Layout

Nominal allocations, before APA/filesystem overhead:

| Partition | MiB | Purpose / filesystem |
|---|---:|---|
| `__mbr` | 128 | APA master / reserved boot area |
| `__net` | 128 | PFS |
| `__system` | 256 | PFS, published system and boot files |
| `__sysconf` | 512 | PFS, system configuration/fonts |
| `__common` | 1024 | PFS, initially empty |
| `__linux.9` | 2048 | Empty EXT2 reserve; English installation |
| `__linux.1` | 512 | EXT2 system root |
| `__linux.4` | 512 | EXT2 Navigator files and English overlay |
| `__linux.5` | 512 | EXT2 data/configuration |
| `__linux.7` | 256 | EXT2 applications |
| `__linux.6` | 128 | Empty EXT2 workspace |
| `__linux.2` | 128 | Linux swap |
| `__linux.8` | 512 | FAT32, `MusicCh/contents` created |
| `__contents` | 128 | PFS, published initial content tree |

The optional separate PS2 Linux installation (`__linux.3`) is omitted.
The music and download partitions provide test capacity, not large libraries.
An independent MBR boot payload has not been installed; the intended entry is
a standalone ELF loading the published `__system/p2lboot/osdboot.elf` path.

## Host verification completed

- Both input archives matched the creator archive's published SHA1 values.
- All image APA headers passed checksum and file-bound checks.
- Every EXT2 filesystem passed a forced read-only filesystem check.
- Up to five largest regular files per populated EXT2 filesystem were dumped
  independently and compared with the source files.
- Every PFS regular file in `__system`, `__sysconf` and `__contents` was exported
  independently and hash-compared with the published source.
- Every generated EXT2/FAT32/swap payload was reread through its image mapping
  and matched its original SHA256 before finalizing the image.

Local files:

- `private/psbbn-v4.3-eng-7g.img`
- `private/verification-7g.json`
- `private/COMPACT-IMAGE-INFO.txt`

The v0.4 hardware PASS covers its synthetic 8 MiB test disk. It does not test
this 7 GiB image, its filesystems, offsets above 4 GiB, or PSBBN boot. Real-image
preflight and boot-environment integration are the next hardware gates.
