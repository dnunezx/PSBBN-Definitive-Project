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
file psbbn_suite.elf
mips64r5900el-ps2-elf-readelf -h psbbn_suite.elf
file psbbn_real.elf
mips64r5900el-ps2-elf-readelf -h psbbn_real.elf
python3 tests/test_real_profile.py build/real-test-checks.bin
python3 tests/check_real_modules.py
mkdir -p dist/PSBBN
rm -f dist/ata_probe.elf
cp psbbn_suite.elf psbbn_real.elf dist/
cp tests/probe.dat dist/PSBBN/
python3 tests/make_test_disk.py dist/PSBBN/diagnostic.img
cp README.md SUITE.md REAL-IMAGE.md BOOT.md VENDOR.md SMALL-IMAGE.md THIRD-PARTY-NOTICES.md dist/
mkdir -p dist/tests
cp tests/make_real_checks.py dist/tests/
cp -R licenses dist/
sha256sum "$PS2SDK"/iop/irx/{iomanX,fileXio,sio2man,mmceman,bdm,bdmfs_fatfs,usbd,usbmass_bd}.irx > dist/SDK-MODULE-SHA256SUMS
sha256sum build/{mmcefhi,atad_emu,suite_rpc}.irx > dist/SUITE-MODULE-SHA256SUMS
sha256sum build/{mmcefhi,atad_ro,apa_ro,pfs_ro,suite_rpc}.irx > dist/REAL-MODULE-SHA256SUMS
(cd dist && sha256sum psbbn_suite.elf psbbn_real.elf PSBBN/probe.dat PSBBN/diagnostic.img > SHA256SUMS)
