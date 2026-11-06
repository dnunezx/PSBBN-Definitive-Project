#!/usr/bin/env bash
set -euo pipefail
# Run in the pinned PS2SDK container after kernel/build.sh has produced vmlinux.
test -s build/linux/vmlinux || { echo 'Build the Linux kernel first; see BOOT.md.' >&2; exit 1; }
make psbbn_boot.elf
mkdir -p build/boot-sdk boot-dist/PSBBN
for module in sio2man mcman-2000 mcserv-2000 padman-2000; do
    cp "$PS2SDK/iop/irx/$module.irx" build/boot-sdk/
done
python3 tests/check_boot_build.py
python3 tests/check_cdvd_tables.py build/linux/vmlinux
python3 tests/test_mode_gate.py psbbn_boot.elf
cp psbbn_boot.elf BOOT.md VENDOR.md THIRD-PARTY-NOTICES.md boot-dist/
cp -R licenses boot-dist/
sha256sum build/boot-sdk/{sio2man,mcman-2000,mcserv-2000,padman-2000}.irx build/{mmcefhi,atad_checked,block_rpc,intrelay}.irx > boot-dist/BOOT-MODULE-SHA256SUMS
sha256sum build/linux/vmlinux vendor/tge/sbios/sbios_new.bin > boot-dist/KERNEL-SBIOS-SHA256SUMS
(cd boot-dist && sha256sum psbbn_boot.elf > SHA256SUMS)
