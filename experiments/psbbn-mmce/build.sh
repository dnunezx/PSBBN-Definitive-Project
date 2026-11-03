#!/usr/bin/env bash
set -euo pipefail
echo "PS2DEV=${PS2DEV:?} PS2SDK=${PS2SDK:?}"
command -v make
command -v mips64r5900el-ps2-elf-gcc
mips64r5900el-ps2-elf-gcc --version
test -f "$PS2SDK/samples/Makefile.eeglobal"
make clean
make
test -s ata_probe.elf
file ata_probe.elf
mips64r5900el-ps2-elf-readelf -h ata_probe.elf
mkdir -p dist/PSBBN
cp ata_probe.elf dist/
cp tests/probe.dat dist/PSBBN/
cp README.md INVESTIGATION.md THIRD-PARTY-NOTICES.md dist/
mkdir -p dist/tests
cp tests/results.md dist/tests/
cp -R licenses dist/
sha256sum "$PS2SDK"/iop/irx/{iomanX,fileXio,sio2man,mmceman}.irx > dist/SDK-MODULE-SHA256SUMS
(cd dist && sha256sum ata_probe.elf PSBBN/probe.dat > SHA256SUMS)
