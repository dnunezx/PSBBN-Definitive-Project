# Boot investigation — November 4, 2026

## v0.6 implementation update

The kernel-side bridge and a combined preflight/boot-attempt ELF are now built.
See [BOOT.md](BOOT.md). This path boots the source-built kernel directly with
TGE SBIOS; it does not execute osdboot or require Neutrino EE core residency.
The kernel retains original `/dev/hdaN` numbering through a new block driver,
uses the existing APA extent mapper, and sends I/O to Neutrino ATAD via SIF RPC.
No hardware attempt or successful PSBBN boot is recorded yet.

The original findings below remain the reason the direct-boot path was chosen.

v0.5 implements the real-image preflight, not a PSBBN boot environment.
The original interface still needs both a loader reset solution and a Linux
storage solution. The latter cannot be assumed to use the IOP ATAD exports.

## Evidence from the exact local kernel

The `__system/p2lboot/vmlinux` in the verified 4.3.0 archive is an ELF32
little-endian MIPS kernel, 3,098,536 bytes. Its embedded version identifies
Linux `2.4.17_mvl21`, build #63, July 2, 2025. It retains a symbol table.
These are local binary observations, not claims about another kernel version.

- `ps2_ide_select` at `0x800f3810` constructs addresses `0xb400004e`,
  `0xb400002c` and `0xb400002e`; it performs byte loads/stores and `sync`.
  This is a direct hardware-register path in EE Linux code.
- `ps2_ide_dmaproc` at `0x800f3df8` and `ps2_ide_build_dmatable` at
  `0x800f3b40` implement the PS2 IDE DMA path.
- `ps2_ide_setup_dma` at `0x801e3460` also binds SIF RPC services `0x2000`
  and `0x2001`, calls RPC, and waits for completion. Storage integration
  must account for this interface as well as register accesses.

The v0.4 proof is the MMCE -> FHI -> **IOP ATAD export** path. No evidence
shows that it intercepts these Linux register accesses or supplies the Linux
DMA services. A preflight PASS therefore cannot establish Linux storage.

Local analysis files are ignored under `private/`: `kernel-symbols.txt`,
`kernel-ide-disassembly.txt`, `osdboot-disassembly.txt`. No Sony payload or
disassembly is included in public packages.

## Neutrino residency review

The pinned checkout's `ee/ee_core/src/main.c` installs kernel hooks, handles
LoadExecPS2 via its resident core, and calls `New_Reset_Iop2` before loading
each ELF. `iopmgr.c` catches reset DMA requests, supplies its own IOPRP image,
and reloads its configured extra modules after reset.

That is a concrete candidate for retaining MMCE/FHI/ATAD during the EE loader
phase. It is not integrated into v0.5, and it does not establish that those
hooks or low-memory module storage survive Linux taking over the EE.
The core's default placement starts at `0x00084000`; its modules begin at
`0x00095000`. Audit those against kernel segments and boot-time memory clears.
The Sony loader occupies `0x01800000` through `0x018c4b48`.

The exact kernel has a PT_LOAD segment from `0x80010000` to `0x80228f50`
(physical RAM aliases `0x00010000` through `0x00228f50`), entry `0x80010490`.
This overlaps the default Neutrino resident core and module area. Preserving
those addresses through the loader phase does not preserve them after this
kernel loads.

## Next work

1. Start with the project's credited
   [published kernel source](https://github.com/CosmicScale/PSBBN-Definitive-Patch-Kernel)
   and match its PS2 IDE/DMA driver and Linux boot ABI to this binary.
   Determine the smallest kernel-side redirect to an MMCE-backed IOP
   block RPC service reusing the existing FHI/ATAD backend.
2. Specify the IOP module set and lifetime across loader resets and Linux
   startup, including SIF DMA buffers and any write requirements.
3. Integrate and verify resident reset handling before enabling a boot attempt.
   Passing v0.5 only authorizes conclusions about the tested reads/filesystems.

No direct jump or unverified `--boot` option was added. No additional hardware
test is requested merely to demonstrate a loader that lacks Linux storage.
