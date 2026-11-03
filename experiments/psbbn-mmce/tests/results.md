# Hardware results

## Report 3: v0.4 all 11 groups PASS

- User photograph shows `PSBBN AUTOMATIC SUITE v0.4 - FINISHED`.
- PASS: 512-byte fixture; file random/mixed reads; full 8 MiB file scan;
  file repeat/seek stability; handle + IOP reboot; FHI capacity; FHI random/full
  scan; virtual ATA capacity; ATA random/full scan; request bounds checks;
  MMCE reinitialization after ATA.
- Verified rates shown: filesystem 1220 KiB/s, FHI 1060 KiB/s, ATA 1060 KiB/s.
  These rates include verification and RPC overhead.
- Displayed USB report: mass0:/PSBBN-DIAG-v04.log. Full log not inspected yet.
- Source of supplied suite: be689195b37de41bf3ab925f0e82522ae10658ea.
- Confirmed: exact synthetic-image reads through MMCE, FHI and Neutrino ATAD;
  open-handle continuity across the tested IOP reboot; post-ATA MMCE recovery.
- Not established: APA/PFS mounting, real PSBBN image access, writes, offsets
  above 4 GiB, concurrent I/O, PSBBN boot. Exact model/firmware remain unspecified.
- User reports no existing PSBBN HDD image and requests obtaining one next.

## Report 2: v0.3 fixture PASS

- User photograph shows `PSBBN MMCE DIAGNOSTIC v0.3`.
- IOMANX: id 25, start 0; FILEXIO: id 26, start 0.
- SIO2MAN: id 27, start 0; MMCEMAN: id 28, start 0.
- fileXioInit: 0. Devices: tty, rom, cdrom, mmce.
- mmce0 PING: protocol 1, product 1, revision 1 (raw identifiers).
- Open: mmce0:/PSBBN/probe.dat. Reported size: 0x200 (512 bytes).
- Read: 512/512 bytes. CRC32: 1c613576.
- First 32 bytes: 00 through 1f. Full fixture comparison: PASS.
- Console model, memory-card identity/firmware, ELF hash: awaiting details.
- Confirmed: standalone MMCE file access and exact fixture bytes on this setup.
- Not established: virtual ATA, large-image access, APA/PFS, PSBBN boot.

## Report 1: v0.2 photograph

- IOMANX: id 25, start 0; FILEXIO: id 26, start 0.
- SIO2MAN: id 27, start 0; MMCEMAN: id 28, start 0.
- fileXioInit: 0. Devices: tty, rom, cdrom, mmce.
- Both mmce0:/PSBBN/probe.dat and mmce1:/PSBBN/probe.dat: open -1.
- User confirms fixture placement and MMCE enabled.
- Model, firmware, slot, ELF hash: not supplied.
- No storage read or PSBBN boot confirmed. v0.2's open-flag bug was found
  during investigation; repeat with v0.3 before drawing a transport conclusion.

## Next report

The v0.4 summary is recorded above. Future reports should include the final
summary or USB log, image identity, and console/card model and firmware details.

No hardware success or PSBBN boot is claimed by a successful build.

- Date / tester:
- ELF SHA256 / source commit:
- Mode: fixture / --file / --inherit direct ATAD / --hdd
- Slim SCPH model / region:
- MemCard PRO2 firmware / MMCE enabled / slot:
- ELF launcher / launch path / full arguments:
- Module IDs and start results:
- Per-slot MMCE PING / protocol / product / revision:
- Root-directory open (after a failed file open):
- Fixture path / byte count / CRC32 / PASS or FAIL:
- Neutrino release or commit / module set (inherited modes):
- Image path / size / SHA256:
- Expected sector-0 CRC32 from PC:
- ATAD helper load / RPC / read results:
- ATAD exists / packet / total sectors / security:
- Returned sector CRC32 / first 32 bytes:
- FILEXIO / hdd0 results (--hdd):
- Last visible line if hanging:
- Photograph / logs:
