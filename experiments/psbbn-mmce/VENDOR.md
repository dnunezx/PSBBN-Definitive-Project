# Pinned storage sources

The suite reuses Neutrino's ATA implementation; no second emulator is added.
All vendor sources are isolated inside this experiment.

- `vendor/atad`: Neutrino `iop/atad_emu` and `iop/common/mprintf.h`, commit
  `1bff25bcfa02e192f5b35cadbe53648786813974` from
  https://github.com/ps2max32/neutrino. AFL 3.0; see included license.
- `vendor/common`: that Neutrino commit's FHI and configuration headers.
- `vendor/mmcefhi`: MMCE FHI branch, commit
  `8f6c52c02da04a312509a654b12afba27ed437ae` from
  https://github.com/rickgaiser/mmceman/tree/fhi. MIT and retained PS2SDK AFL 2.0
  file notices; both license texts are included.

Local adaptations:

1. Separate Makefiles place generated objects inside `build/` and use current
   PS2SDK rules. Neutrino's local ATAD header precedes SDK compatibility aliases.
2. `fhi_fileid.h` forwards to the reviewed Neutrino `fhi_fileid_config.h`; the
   packed six-file configuration is unchanged (80 bytes).
3. MMCE export-table helper functions follow the table, as required by current
   PS2SDK `iopfixup`. The MMCE imports include `memcpy` for current GCC output.
4. The upstream FHI semaphore was never created. Local `mmce_fhi_init/deinit`
   creates/deletes it; module startup checks initialization before exporting FHI.

ATA transfer logic is unchanged. The diagnostic helper only invokes read paths,
validates request bounds, and reports data for independent byte comparison.
The matched modules are built from these sources and embedded in the suite ELF.
See `SUITE-MODULE-SHA256SUMS` in the download for the built module hashes.
