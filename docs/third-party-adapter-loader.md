# Third-party HDD adapter loader component

This is the first extracted Nexus compatibility component. It does **not** add
an installer option or establish complete third-party adapter support yet.
Normal Definitive installation and the official-adapter path are unchanged.

## What it changes

The original loader initializes Ethernet before starting Linux. Some HDD-only
aftermarket adapters do not provide the Ethernet hardware it expects. This
component reproduces Nexus's exact loader hook: after unpacking, bypass the two
identified SMAP initialization calls and retain the existing loader success
patch. HDD initialization and the four arguments passed to `ExecPS2` remain.

The tool changes one 52-byte hook at file offset 840 (`0x348`), mapped at
`0x018002c8`. The packed inner program, ELF layout and file size remain unchanged.
The hook guards its first runtime call instruction before making three writes:

| Address | Replacement |
| --- | --- |
| `0x01001e28` | `addiu v0,zero,0` |
| `0x01002340` | `addiu v0,zero,0` |
| `0x01009f80` | `addiu v0,zero,1` (existing success patch retained) |

Only the exact 806,908-byte input and its exact patched counterpart are accepted:

- Original SHA-256: `8867ee33c4ecb79ace6fe88b0b4f0148239102544385f0dcfba5bc7737fe1f18`
- Patched SHA-256: `3baad7a3c024ca0da6f10e58026c1943dea00d45f7e0f0390ee91f4ae0f6ab32`

ELF mapping and hook instructions are checked in addition to the complete file
hash. Different loader versions are rejected. There is no option to skip these
checks. Applying again, or restoring an original loader, preserves the bytes.

## Ordinary-file use

Use Python 3.8 or newer; only the standard library is required. On Windows,
use `python` in place of `python3` where appropriate. Supply your own supported
loader file; no Sony loader or private HDD image is included in this component.

```sh
python3 scripts/helper/third_party_adapter_loader.py inspect /path/to/osdboot.elf
python3 scripts/helper/third_party_adapter_loader.py apply /path/to/osdboot.elf /path/to/osdboot.compat.elf
python3 scripts/helper/third_party_adapter_loader.py restore /path/to/osdboot.compat.elf /path/to/osdboot.restored.elf
```

The original input is read-only. Apply/restore require a new output file and
refuse existing paths. Output is flushed and read back. Device paths, nonregular
inputs and symlinks are rejected. The tool does not open physical disks, edit
partition tables, mount filesystems or install its output.

The Hitachi-specific Nexus raw-sector updater is deliberately not part of this
port. Later installer integration must place the verified loader through the
normal file-installation path and retain a known recovery copy.

## Checks and compatibility boundary

Run the tests on an ordinary checkout:

```sh
python3 -m unittest discover -s tests -p test_third_party_adapter_loader.py -v
```

CI uses generated, clearly synthetic ELF fixtures. It checks apply/restore,
exact change bounds, full hashes, file/device guards, output preservation and
failure cleanup. A small instruction model executes the actual hook words for
256 register sets, including branch delay slots, argument preservation and
the no-write path when the first runtime call does not match.

These are host checks, not physical adapter emulation. Nexus separately recorded
an emulator boot with this exact patched loader, and the user reported physical
dashboard entry on one setup. Reliable read/write/flush behavior, dashboard
sound, game launch/return and other adapter revisions remain validation gates.

Local extraction validation passed on Windows and Linux: 16 host tests (the
Windows account skipped POSIX FIFO and symlink creation checks), including 256
hook instruction cases. Against separately retained real loader files, the
command-line tool produced the exact Nexus patched SHA-256 above and restored
the exact original. Exactly 31 byte values change within the 52-byte hook.
Neither reference file was modified; no physical device was opened. Real
loader files remain local and are not included in this repository or CI.

The native HDD kernel and startup path still need their separate compatibility
work. This patch alone is not a complete fix. It is intended for an explicitly
selected experimental HDD-only adapter profile; official adapters should keep
their existing Ethernet-capable path. Software cannot supply missing Ethernet.

No performance, appearance, offline-channel archive, MMCE storage or disabled-
audio changes are included.

## Provenance

Adapted from PSBBN-Nexus `tools/LoaderBypassWriter.cs` and its
`loader-smap-bypass-v01` hook specification. The original 13-instruction
replacement is reproduced exactly; drive-specific writing code is excluded.
Reference Nexus source SHA-256:
`a0437a2e72d136ae42d77f35c24f306dcf487433545ed403d8550afd07886221`.
Copyright (c) 2026 PSBBN-Nexus contributors, MIT license, preserved in
[`scripts/licenses/third-party-adapter-loader`](../scripts/licenses/third-party-adapter-loader).
Existing project and runtime components retain their original licenses.
