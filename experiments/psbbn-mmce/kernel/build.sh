#!/usr/bin/env bash
set -euo pipefail
# Run from the experiment root inside psbbn-linux-build. Build on Linux's
# case-sensitive filesystem, never directly on the Windows source checkout.
printf '%s  %s\n' \
  e3990ee12125ae63c703d54c5a834f79a83be9f7daa834e801081da81c60af8a build/toolchains/binutils-2.9EE-cross.tar.gz \
  2970813c5e5bdae69019a3b416f09c1a7dbcb69c8e523118e55ec77b9abcd373 build/toolchains/gcc-2.95.2-cross.tar.gz | sha256sum -c -
tar -C /usr/local -xzf build/toolchains/binutils-2.9EE-cross.tar.gz
tar -C /usr/local -xzf build/toolchains/gcc-2.95.2-cross.tar.gz
/usr/local/ps2/bin/ee-gcc --version
/opt/psbbn-binutils/as --version | head -1
mkdir -p /tmp/psbbn-kernel
tar -C /tmp/psbbn-kernel -xf build/psbbn-kernel-source.tar
source_dir=/tmp/psbbn-kernel/linux-2.4.17_ps2
find "$source_dir" -type f -exec perl -e '
  for my $f (@ARGV) {
    next unless -T $f;
    open(my $in, "<", $f) or die $!; local $/; my $text = <$in>; close($in);
    if ($text =~ s/\r\n/\n/g) { open(my $out, ">", $f) or die $!; print $out $text; close($out); }
  }' {} +
cp kernel/psbbn_mmce.c include/block_rpc.h "$source_dir/drivers/block/"
cp include/boot_trace.h "$source_dir/include/asm-mips/ps2/"
cp kernel/boot_trace.c kernel/boot_trace_font.c "$source_dir/arch/mips/ps2/"
tr -d '\r' < kernel/early-boot.patch > /tmp/early-boot.patch
patch -d "$source_dir" -p1 --batch < /tmp/early-boot.patch
tr -d '\r' < kernel/modern-binutils.patch > /tmp/modern-binutils.patch
patch -d "$source_dir" -p1 --batch < /tmp/modern-binutils.patch
cp "$source_dir/arch/mips/configs/defconfig-ps2" "$source_dir/.config"
sed -i '/include $(TOPDIR)\/Rules.make/i obj-$(CONFIG_PSBBN_MMCE) += psbbn_mmce.o' "$source_dir/drivers/block/Makefile"
sed -i '/comment .Block devices./a\if [ "$CONFIG_PS2" = "y" ]; then\n   bool "PSBBN MMCE virtual HDD" CONFIG_PSBBN_MMCE\nfi' "$source_dir/drivers/block/Config.in"
# Physical IDE and its DEV9/DMA path must be absent from the MMCE kernel.
sed -i -E 's/^CONFIG_IDE=y/# CONFIG_IDE is not set/; s/^CONFIG_BLK_DEV_IDE=y/# CONFIG_BLK_DEV_IDE is not set/' "$source_dir/.config"
# v0.6 hangs while loading SDRDRV on the console. Omit both the IOP sound
# modules and their Linux client for this recovery build; otherwise sound init
# would repeatedly try to bind to the absent service.
sed -i -E 's/^CONFIG_PS2_SD=y/# CONFIG_PS2_SD is not set/; s/^CONFIG_SOUND=y/# CONFIG_SOUND is not set/' "$source_dir/.config"
printf '\nCONFIG_PSBBN_MMCE=y\n' >> "$source_dir/.config"
cd "$source_dir"
tr -d '\r' < /work/kernel/slim-rpc-irq.patch > /tmp/slim-rpc-irq.patch
patch -p1 --batch < /tmp/slim-rpc-irq.patch
make_args=(ARCH=mips CROSS_COMPILE=/usr/local/ps2/bin/ee-
  'CC=/usr/local/ps2/bin/ee-gcc -B/opt/psbbn-binutils/'
  'LD=/opt/psbbn-binutils/ld -m elf32lr5900' AS=/opt/psbbn-binutils/as
  AR=/opt/psbbn-binutils/ar NM=/opt/psbbn-binutils/nm)
set +o pipefail
yes '' | make "${make_args[@]}" oldconfig > /work/build/kernel-config.log 2>&1
config_status=${PIPESTATUS[1]}
set -o pipefail
test "$config_status" -eq 0
grep -q '^CONFIG_PSBBN_MMCE=y$' .config
! grep -q '^CONFIG_PS2_SD=y$' .config
! grep -q '^CONFIG_SOUND=y$' .config
# Preserve the checked-in generated keymap; no host keyboard-layout tool needed.
touch drivers/char/defkeymap.c
make "${make_args[@]}" dep > /work/build/kernel-dep.log 2>&1
make -j8 "${make_args[@]}" vmlinux > /work/build/kernel-compile.log 2>&1
grep -q ' psbbn_mmce_init$' System.map
if grep -q ' ps2_ide_select$' System.map; then echo 'Physical IDE was unexpectedly linked' >&2; exit 1; fi
mkdir -p /work/build/linux
cp vmlinux System.map .config /work/build/linux/
cp drivers/video/ps2con.c /work/build/linux/ps2con-boot-source.c
# Retain the exact assembly and both objects for a legacy/current comparison.
make "${make_args[@]}" drivers/ps2/cdvd.s > /work/build/cdvd-assembly.log 2>&1
cp drivers/ps2/cdvd.s drivers/ps2/cdvd.o /work/build/linux/
sed 's/\.set[[:space:]]*arch=r5900/.set mips3/g' drivers/ps2/cdvd.s > /work/build/linux/cdvd-legacy.s
/usr/local/ps2/bin/ee-as -mips2 -mcpu=r5900 -EL /work/build/linux/cdvd-legacy.s -o /work/build/linux/cdvd-legacy.o
/opt/psbbn-binutils/objdump -dr drivers/ps2/cdvd.o > /work/build/linux/cdvd-current.txt
/opt/psbbn-binutils/objdump -dr /work/build/linux/cdvd-legacy.o > /work/build/linux/cdvd-legacy.txt
readelf -h vmlinux
sha256sum vmlinux
