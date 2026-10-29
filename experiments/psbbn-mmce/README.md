# PSBBN-MMCE — read-only ATA diagnostic v0.1

Target: PS2 Slim + MMCE-capable MemCard PRO2 + Neutrino.

## Build
With ps2dev/PS2SDK toolchain configured, run `make`.
The result should be `ata_probe.elf`. This source has not been compiled on a PS2SDK toolchain here.

## Runtime prerequisites
Neutrino must launch the ELF with the intended MMCE-backed `-ata0` image.
The probe deliberately does not reset the IOP or load HDD, IOMANX, or FILEXIO modules.
It assumes a working fileXio RPC service is already present. If not, it will report failure or may hang during initialization depending on the IOP setup.

The absence of `hdd0:` does not necessarily indicate that the ATA emulation failed: a working virtual ATA disk still requires an HDD/APA driver exposing `hdd0:`.

## Safety
No writes, formats, partition mutations, or image creation.
The probe uses HDIOC_READSECTOR only, and displays the first 32 returned bytes.

## Launch
Refer to your checked-out Neutrino README for current `-bsd`, `-ata0`, and `-elf` syntax. Do not assume the illustrative MMCE paths are valid on your particular firmware.
Start by testing an ordinary Neutrino MMCE game to verify the MemCard PRO2 backend. Then run this probe from a separate USB device or via ps2link to avoid probe bootstrap ambiguity.

## Important scope limitation
This v0.1 is an HDD filesystem-driver diagnostic, not a direct ATAD RPC probe. A negative `hdd0:` result does not disprove that Neutrino's virtual ATA is working. Sector-read results must be checked against known bytes in the HDD image.
