#!/usr/bin/env bash
set -euo pipefail
test -s build/linux/vmlinux
mkdir -p build/emulator
for module in dev9 atad; do
    cp "$PS2SDK/iop/irx/ps2${module}.irx" "build/emulator/native_${module}.irx"
    "$PS2SDK/bin/bin2c" "build/emulator/native_${module}.irx" "build/emulator/native_${module}.c" "native_${module}_irx"
done
"$PS2SDK/bin/bin2c" private/real-checks.bin build/emulator/checks.c emulator_checks
make -C iop -f Makefile.block IOP_BIN=../build/emulator/block_rpc.irx IOP_OBJS_DIR=../build/emulator/block/ BLOCK_EXTRA_CFLAGS=-DPSBBN_BLOCK_CAPACITY=14680064U
"$PS2SDK/bin/bin2c" build/emulator/block_rpc.irx build/emulator/block_rpc.c block_rpc_irx
for source in build/emulator/{native_dev9,native_atad,checks,block_rpc}.c; do
    mips64r5900el-ps2-elf-gcc -G0 -O2 -Wall -Wextra -Werror -D_EE -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" -c "$source" -o "${source%.c}.o"
done
make -f Makefile -f emulator.mk psbbn_emulator.elf
cp psbbn_emulator.elf build/emulator/
sha256sum psbbn_emulator.elf build/linux/vmlinux vendor/tge/sbios/sbios_new.bin \
    build/emulator/{native_dev9,native_atad,block_rpc}.irx private/real-checks.bin \
    > build/emulator/BUILD-SHA256SUMS
