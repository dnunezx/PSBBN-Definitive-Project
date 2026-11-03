# Existing probe and upstream storage review

## v0.4 virtual ATA hardware milestone

The user's final v0.4 screen shows all 11 groups PASS. Verified rates are
1220 KiB/s through FILEXIO and 1060 KiB/s through each of FHI and ATAD, including
the diagnostic's validation and RPC overhead. This confirms the tested
MMCE -> FHI -> Neutrino ATAD read route and IOP handle transition on hardware.
It does not establish real PSBBN boot compatibility, large offsets or writes.

The current project's installer creates both PFS and EXT2 `__linux.*`
partitions (`scripts/PSBBN-Installer.sh`, partition creation), copies Linux
kernels into `__system/p2lboot`, and creates a `$PSBBN` boot entry with
`-dev9=NICHDD`. These are source observations, not proof that Neutrino ATAD
interception covers the later Linux/PSBBN runtime. That boot/storage path still
requires investigation. The user has no existing disk image and has requested
obtaining one; local Sony files belong only in ignored `private/`, never the
repository or Actions artifacts.

## v0.3 hardware milestone

The second user photograph shows an MMCE PING response on mmce0 (protocol 1,
product 1, revision 1), a 512-byte file size, 512/512 bytes read, CRC32 1c613576,
and `Fixture comparison: PASS`. Basic standalone MMCE file access is confirmed
for that setup. Device identity and firmware are not inferred from the product
code. Direct virtual ATA and PSBBN boot still need separate tests.

## First hardware report and v0.3 correction

The user's v0.2 photograph shows all four modules loaded with start result 0,
`fileXioInit: 0`, and `mmce` registered. Opening the fixture failed with -1 on
both slots. The user confirms the fixture path and MMCE enablement. This proves
ELF execution and module registration, not card communication or a file read.

Inspection found v0.2 passed newlib `O_RDONLY` (0) directly to FILEXIO, which
passes flags unchanged to the IOP. `io_common.h` defines `FIO_O_RDONLY` as 1.
MMCEMAN computes `(flags & 3) - 1` into an unsigned byte; the incorrect 0 becomes
0xff, so the encoded command cannot be described as read-only. No successful
file open, modification, or data read is established by the photograph.
v0.3 uses the IOP flag, adds documented MMCEMAN devctl PING (command 1), and
checks the root directory after failed opens. A corrected hardware retest is
required before attributing the failure to firmware, storage, or a path.

Reviewed 2026-11-03. Starting project commit:
`ae519cf79f1264a0eaa2f74d9f7b705f66bd1ce3` on `probe`.

## What existed

The v0.1 source listed existing IOP devices and queried an already loaded
HDD/APA driver. It never initialized MMCE, installed virtual ATA, or launched
PSBBN. `fileXioInit()` could wait forever without FILEXIO. Its sector-read call
supplied no output buffer, contrary to PS2SDK APA's implementation.

[Failed run 40802315969](https://github.com/dnunezx/PSBBN-Definitive-Project/actions/runs/40802315969)
installed Make but failed before compilation because Bash was absent.
The root-level workflow was an obsolete duplicate outside GitHub's workflow
directory. It is replaced by one active workflow and a shared local/CI script.

## Verified design, not demonstrated PSBBN compatibility

Neutrino reviewed at `1bff25bcfa02e192f5b35cadbe53648786813974`:

- [MMCE config](https://github.com/ps2max32/neutrino/blob/1bff25bcfa02e192f5b35cadbe53648786813974/ee/loader/config/bsd-mmce.toml)
  uses MMCEMAN in the load environment and MMCEFHI in the emulation environment.
- [File interface setup](https://github.com/ps2max32/neutrino/blob/1bff25bcfa02e192f5b35cadbe53648786813974/ee/loader/src/fhi_config.c)
  obtains an MMCE file ID through ioctl2 `0x80` and records its size. This is
  Neutrino's verified usage, not a general MMCE raw-sector API to assume.
- [Loader](https://github.com/ps2max32/neutrino/blob/1bff25bcfa02e192f5b35cadbe53648786813974/ee/loader/src/main.c)
  opens `-ata0` with `O_RDWR` and registers it as `FHI_FID_ATA0`.
- [ATAD emulator](https://github.com/ps2max32/neutrino/blob/1bff25bcfa02e192f5b35cadbe53648786813974/iop/atad_emu/src/ps2atad.c)
  gets capacity through `fhi_size` and transfers through `fhi_read`/`fhi_write`.
  Its ATAD export table is version 1.3.

Together these establish a source-level MMCE-file-to-ATAD route, without
demonstrating it on this hardware or PSBBN. The emulator stubs several ATA
operations, reports success regardless of FHI transfer results, and routes
transfers through ATA0 irrespective of the device argument. Our helper targets
device 0 and exposes data for comparison instead of trusting a zero result.

PS2SDK reviewed at `0f8c56505148ac42fc1f4158d21aa0f61141d905`:

- [APA devctl](https://github.com/ps2dev/ps2sdk/blob/0f8c56505148ac42fc1f4158d21aa0f61141d905/iop/hdd/apa/src/hdd_fio.c)
  sends `HDIOC_READSECTOR` data to output buffer `bufp`.
- [FILEXIO init](https://github.com/ps2dev/ps2sdk/blob/0f8c56505148ac42fc1f4158d21aa0f61141d905/ee/rpc/filexio/src/fileXio_rpc.c)
  retries binding without an absent-server timeout.

The compiled SDK is from the Dockerfile's pinned image. These review commits
are source references, not a claim that the image was built from those commits.
`SDK-MODULE-SHA256SUMS` identifies the embedded modules.

[MMCEMAN docs](https://github.com/ps2-mmce/mmceman/blob/de28936fdf45fe445554cfa0b8f058b05fa8212f/README.md)
document `mmce0:/`, `mmce1:/`, and a compatible SIO2MAN requirement.
The standalone ELF embeds SDK SIO2MAN. The pinned container supplies MMCEMAN,
but **not** `mmcefhi.irx`: it builds this diagnostic, but is not yet a complete
matched Neutrino build environment. Resolve that dependency before a rebuild.

For v0.4 the MMCEFHI dependency is now built from the pinned FHI branch under
`vendor/`, alongside Neutrino's existing ATAD module. See `VENDOR.md` for the
source revisions and required build/lifecycle fixes. The suite tests this path
directly; it does not require a full rebuild of Neutrino's loader.

## Open hardware and boot questions

1. Fixture PASS is confirmed in report 2; exact model/firmware details are pending.
2. The self-contained suite confirms driver loading and its IOP transition;
   the full Neutrino loader and PSBBN's own transitions remain untested.
3. ATAD capacity and exact synthetic-image bytes are confirmed in report 3.
4. Which HDD/APA/PFS, identity and security operations does the user's PSBBN
   version require, including its entry point, boot arguments and IOP resets?
5. Which module substitutions survive PSBBN's own module loading?
6. Do large offsets, errors, reads/writes, and latency work sufficiently?

No APA/PFS driver is automatically loaded here. PSBBN entry points and Sony
module requirements have not been established, and no Sony files are included.
The remaining boot-layer tests require separate results beyond report 3.
