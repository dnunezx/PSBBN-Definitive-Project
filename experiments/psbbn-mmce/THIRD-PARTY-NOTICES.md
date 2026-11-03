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

The new `ata_read_rpc` helper is a read-only client of an already loaded ATAD
library. Neutrino itself is neither embedded nor redistributed by this package.
