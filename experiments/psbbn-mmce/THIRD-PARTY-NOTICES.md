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
