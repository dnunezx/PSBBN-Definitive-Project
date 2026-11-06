# Third-party components

This diagnostic embeds unmodified PS2SDK `iomanX.irx`, `fileXio.irx`,
`sio2man.irx`, and MMCE `mmceman.irx` from the SDK container pinned in
`Dockerfile`. Their hashes are recorded in `SDK-MODULE-SHA256SUMS`.
It also links PS2SDK libraries. No Sony PSBBN executables or modules are included.

- PS2SDK: Copyright PS2DEV contributors. Academic Free License 2.0;
  see `licenses/PS2SDK-AFL-2.0.txt` and https://github.com/ps2dev/ps2sdk.
- MMCEMAN: Copyright (c) 2024 MMCE Authors. MIT License;
  see `licenses/MMCEMAN-MIT.txt` and https://github.com/ps2-mmce/mmceman.
  Its source files also retain PS2SDK AFL 2.0 notices.

The suite also embeds SDK BDM/FATFS/USB modules and builds MMCEFHI and Neutrino's
ATAD emulator from pinned source. See `VENDOR.md` for exact commits and changes.
Neutrino components are copyright their contributors and licensed under AFL 3.0;
see `licenses/Neutrino-AFL-3.0.txt`. Source is included in this repository at
https://github.com/dnunezx/PSBBN-Definitive-Project/tree/probe/experiments/psbbn-mmce/vendor.
The diagnostic RPC helpers consume the existing drivers' read interfaces.

The v0.5 preflight additionally embeds adapted PS2SDK APA/PFS drivers from the
source revision in `VENDOR.md`, under AFL 2.0, and a read-only build of Neutrino
ATAD under AFL 3.0. Their source and original copyright notices are included.

The v0.6.9 boot ELF embeds a rebuilt GPL 2.0 PSBBN Linux kernel, TGE SBIOS and
interrupt relay (AFL 2.0). The v0.6 SDK SDRDRV adaptation (AFL 2.0) remains in
source but is omitted from this sound-disabled recovery ELF. Exact revisions
and adaptations are in `VENDOR.md` and `BOOT.md`. The companion source bundle
includes the kernel archive and local source/build changes. The GPL text is
`licenses/KernelReloaded-GPL-2.0.txt`; TGE's license is also retained at
`vendor/tge/LICENSE`. No proprietary Sony boot executable or disk image is
included in the boot ELF or public-source bundle.

The early kernel trace includes the PS2SDK debug font (AFL 2.0), retained with
its copyright notice in `kernel/boot_trace_font.c`. Its early renderer
and local kernel instrumentation are included in the corresponding source ZIP.
