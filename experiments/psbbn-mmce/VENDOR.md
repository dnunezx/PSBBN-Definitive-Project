# Dependencies and local changes

## Pinned sources

- PS2SDK container: `ghcr.io/ps2dev/ps2dev@sha256:26832a6865b890927632734703e25faa24da1217dd1ea17dcedbddd7c7683be3`.
  SDK libraries/modules use AFL 2.0; MMCEMAN uses MIT with retained SDK notices.
- `vendor/atad` and `vendor/common`: [Neutrino](https://github.com/ps2max32/neutrino),
  commit `1bff25bcfa02e192f5b35cadbe53648786813974`, AFL 3.0.
- `vendor/mmcefhi`: [MMCE FHI](https://github.com/rickgaiser/mmceman/tree/fhi),
  commit `8f6c52c02da04a312509a654b12afba27ed437ae`, MIT/AFL 2.0.
- `vendor/{apa,libapa,pfs,libpfs,sdrdrv}`: PS2SDK source at
  `0f8c56505148ac42fc1f4158d21aa0f61141d905`, AFL 2.0.
- Kernel: [PSBBN Definitive Patch Kernel](https://github.com/CosmicScale/PSBBN-Definitive-Patch-Kernel),
  commit `80b14105512ed6114c1defa8e9dee40679072874`, GPL 2.0.
- `vendor/tge` and `vendor/intrelay`: kernelreloaded/Pcsx2-lnx commit
  `eb5a2a23dc9894322aea571242365460241eb9f6`, retained AFL 2.0 notices.

## Storage adaptations

Makefiles target the pinned SDK and put generated objects in `build/`. MMCEFHI
creates its FHI semaphore, validates seeks and supports checked handle closure.
A failed seek blocks transfers; a failed close retains the handle.

MMCEFHI selects the modern SIO2MAN export table before the legacy same-name
alias and relinks its callers. This ensures the shipped v2 PADMAN/MCMAN imports
use the controller/card sharing locks. Legacy-only fallback is retained.

Read-only ATAD rejects writes. Checked ATAD validates ranges and propagates FHI
read/write errors. APA uses ATAD without physical DEV9 imports; preflight APA/PFS
journal recovery/reset is disabled and dirty journals cause failure.

The block RPC service connects Linux to Neutrino ATAD. The Linux driver retains
APA partition mapping, serializes requests and preserves buffers after a timeout.
Physical IDE is disabled. The kernel includes the Slim RPC interrupt patch,
early boot diagnostics, graphical console handoff and current-binutils/O32 fixes.

## SBIOS adaptations

TGE configuration reads retain OPENCONFIG's validated count and copy only
15 bytes per requested block. RPC completion ignores null clients and completions
with no pending packet; legacy replies need not echo packet addresses.

Sound is disabled in the hardware boot build. The adapted SDRDRV source remains
available but neither SDRDRV nor LIBSD is embedded in this build.

## Build records and licenses

Build packages include module/kernel/SBIOS SHA256 manifests. The companion
source ZIP includes the complete pinned kernel source and local build changes.
Original source notices are retained; license texts are in `licenses/` and
`vendor/tge/LICENSE`. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
