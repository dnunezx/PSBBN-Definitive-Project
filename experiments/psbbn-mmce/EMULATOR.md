# PCSX2 boot test

This separate test replaces the MemCard PRO2/MMCE transport with PCSX2's native
DEV9 HDD and PS2SDK ATAD. It retains the current Linux kernel, block RPC protocol,
SBIOS and privileged handoff. It does not validate MMCE hardware or timing.

Build `psbbn_emulator.elf` with `bash build-emulator.sh` in the existing
`psbbn-probe-build` container. The build needs the private matching
`private/real-checks.bin`, which is embedded to check the virtual HDD's capacity
and all 66 sample fingerprints before Linux starts. This launcher is deliberately
separate from the hardware preflight and does not claim its MMCE checks passed.

Use a private disposable copy of `psbbn-v4.3-eng-7g.img`, with 1 MiB of zero
padding appended. Stock PCSX2 2.6.3 rejects the read ending at the original final
sector without this padding. The emulator-only block service caps its reported
capacity and request bounds at the original 14,680,064 sectors, hiding the padding
from Linux. The PC original is not modified.

Use a separate portable PCSX2 installation, enable its DEV9 HDD and select the
copy. Disable the EE recompiler. Enable PINE on localhost port 28011 to read the
kernel ring buffer with `python tests/read_emulator_log.py --tail`. This uses
the current `build/linux/System.map` and KSEG0 addresses, so it does not rely on
desktop input or screenshots. Do not read unmapped user addresses through PINE:
the emulator's memory reader can raise a guest fault from its debugger thread.

`--snapshot` requests the emulator's native slot-0 save state. The archive includes
its own `Screenshot.png`, CPU state and RAM. This captures the guest without
activating a window or controlling the user's mouse/keyboard. The archive uses
ZIP Zstandard; current 7-Zip can extract it. Select the newest `.p2s` file because
each launcher CRC has a separate filename.

## November 6 results

- Stock PCSX2 2.6.3 passed all 66 virtual HDD samples with the padding workaround.
- Linux initialized the bridge and discovered the PSBBN APA partitions.
- The CD/DVD initcall completed, exercising the v0.6.9 assembler repair.
- The next stall exposed a real SBIOS bug: READCONFIG copied the RPC's entire
  1,024-byte payload to a 45-byte Linux settings buffer. This erased `sk_cachep`
  and neighboring globals, then `inet_init` faulted in `kmem_cache_alloc`.
- `vendor/tge/sbios/scmd.c` now remembers OPENCONFIG's block count and copies
  only 15 bytes per requested block on a successful read. Invalid counts and
  failed reads cannot copy a payload. No configuration write is implemented.
- The guarded-buffer regression passes for 0..68 blocks and rejects a mutation
  restoring the 1,024-byte copy. Normal and capped block RPC tests also pass.
- With the SBIOS fix, Linux completes network initialization, mounts the ext2
  root filesystem and reaches `Freeing unused kernel memory: 68k freed`.
- The ordinary emulator stalls at userspace entry. The separate compatibility
  build passes this point and starts the PSBBN `bn` process (PID 239 in the first
  run), enables swap and loads userspace-requested modules. A userspace program
  opens the GS device; the saved guest image was still black.
- A later completion for the block RPC client had no outstanding packet. TGE's
  `_request_end` then called `rpc_packet_free(NULL)`, panicking in interrupt
  context. It now ignores null clients and completions without an outstanding
  packet. A host test covers normal, legacy bind and duplicate completions.
- With that guard, the clean-image retry reaches the English PlayStation BB
  graphical startup screen: "Updating the content management information...".
  This is the first observed PSBBN graphical output in this experiment. The
  update then completes and the interactive English network setup screen
  appears, with a highlighted Start button and Back/Enter prompts. Boot to
  PSBBN graphical setup is confirmed. Network setup and the main menu were not
  exercised; no desktop/controller input was injected.

The tested emulator launcher includes the SBIOS fix. The updated hardware
`psbbn_boot.elf` is prepared as v0.6.10 with these SBIOS fixes; hardware testing
is pending. Older v0.6.9 archives remain preserved.

The separate patched emulator uses PCSX2 commit
`337daf7ed9a1529da26abdcbef996407b8071145` and the bootloader project's existing
`ee-tlb-fixes`, `syscall-guard`, `ata-pio-write`, `dev9-trace-gating`,
`branch-delay-ratelimit` and `cache-mode-ratelimit` patches. Its source and build
are ignored under `.upstream/pcsx2-linux-test`; official PCSX2 Windows build
libraries were downloaded rather than rebuilt. No proprietary image/BIOS or
generated emulator binary is committed.

The additional local `kernel/pcsx2-linux-signature.patch` restricts the
compatibility build's Linux syscall bypass to guests with the TGE `PS2b`
signature installed. This alone did not resolve its launcher stall. The working
launcher also replaces its SDK timer sleeps while polling block RPC with bounded
busy polling, only under `PSBBN_EMULATOR_BUILD`. The compatibility run uses EE and
IOP interpreters and disables IntcStat/WaitLoop speedhacks. Desktop input sources
are disabled in its isolated configuration.
