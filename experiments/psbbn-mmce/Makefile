EE_BIN = ata_probe.elf
EE_OBJS = src/ata_probe.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman.o build/ata_read_rpc.o
EE_LIBS = -ldebug -lfileXio -lpatches -lkernel
EE_CFLAGS = -Wextra -Werror
EE_INCS = -Iinclude
SUITE_OBJS = src/suite.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman.o build/mmcefhi.o build/atad_emu.o build/suite_rpc.o build/bdm.o build/bdmfs_fatfs.o build/usbd.o build/usbmass_bd.o
REAL_OBJS = src/real_image.o $(filter-out src/suite.o build/atad_emu.o,$(SUITE_OBJS)) build/atad_ro.o build/apa_ro.o build/pfs_ro.o
BOOT_OBJS = src/boot_main.o src/boot_handoff.o $(filter-out src/real_image.o,$(REAL_OBJS)) build/atad_checked.o build/block_rpc.o build/intrelay.o build/linux_mcman.o build/linux_mcserv.o build/linux_padman.o build/linux_kernel.o build/linux_sbios.o
SOUND_BOOT_OBJS = $(filter-out src/boot_main.o build/linux_kernel.o,$(BOOT_OBJS)) src/sound_boot_main.o build/sound_linux_kernel.o build/libsd.o build/sdrdrv.o build/collection_ata.o build/apa_sync.o build/pfs_sync.o build/collection_assets.o
COMBINED_BOOT_OBJS = $(filter-out src/boot_main.o build/mmceman.o,$(BOOT_OBJS)) src/combined_boot_main.o build/mmceman_scan.o build/collection_ata.o build/apa_sync.o build/pfs_sync.o build/collection_assets.o
.DEFAULT_GOAL := all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

.PHONY: all clean
all: $(EE_BIN) psbbn_suite.elf psbbn_real.elf

psbbn_boot.elf: $(BOOT_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(BOOT_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 $(EE_LIBS)

psbbn_mmce_soundtest.elf: $(SOUND_BOOT_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(SOUND_BOOT_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 $(EE_LIBS)

psbbn_mmce_combined.elf: $(COMBINED_BOOT_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(COMBINED_BOOT_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 $(EE_LIBS)

PERF_BOOT_OBJS = $(filter-out src/combined_boot_main.o,$(COMBINED_BOOT_OBJS)) src/perf_boot_main.o
psbbn_mmce_perf.elf: $(PERF_BOOT_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(PERF_BOOT_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 $(EE_LIBS)
src/perf_boot_main.o: src/real_image.c src/boot_launch.h src/mmce_scan.h src/collection_sync.h include/perf_trace.h include/collection_io.h include/mmce_directory.h include/iso_scan.h include/mmce_catalog.h include/boot_elf.h include/boot_trace.h include/block_rpc.h include/real_checks.h include/suite_rpc.h vendor/common/fhi_fileid_config.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ivendor/common -DPSBBN_BOOT_BUILD -DPSBBN_COMBINED_BUILD -DPSBBN_PERF_TRACE -c $< -o $@

NATIVE_TEST_OBJS = src/native_probe.o build/native_runtime.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman_scan.o
psbbn_native_test.elf: $(NATIVE_TEST_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(NATIVE_TEST_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 -Wl,--defsym,_heap_size=0x100000 $(EE_LIBS)
src/native_probe.o: src/native_probe.c include/native_launch.h include/mmce_catalog.h include/iso_scan.h build/native-runtime/runtime.h
src/native_probe.o: EE_CFLAGS += -Wno-unused-function
src/native_probe.o: EE_INCS += -Ibuild/native-runtime
NATIVE_PERF_OBJS = $(filter-out src/native_probe.o,$(NATIVE_TEST_OBJS)) src/native_perf.o
psbbn_native_perf.elf: $(NATIVE_PERF_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(NATIVE_PERF_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 -Wl,--defsym,_heap_size=0x100000 $(EE_LIBS)
src/native_perf.o: src/native_probe.c include/perf_trace.h include/native_launch.h include/mmce_catalog.h include/iso_scan.h build/native-runtime/runtime.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Wno-unused-function -Ibuild/native-runtime -DPSBBN_PERF_TRACE -c $< -o $@
src/native_probe.o src/native_receiver.o src/native_perf.o: include/native_boot.h include/native_console.h
src/combined_boot_main.o src/perf_boot_main.o src/boot_main.o src/sound_boot_main.o: include/native_console.h
src/native_probe.o src/native_receiver.o src/combined_boot_main.o src/sound_boot_main.o src/boot_main.o src/real_image.o: include/perf_trace.h
build/native_runtime.o: build/native-runtime/runtime.c build/native-runtime/runtime.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

NATIVE_RECEIVER_OBJS = src/native_receiver.o src/native_receiver_entry.o $(filter-out src/native_probe.o,$(NATIVE_TEST_OBJS))
psbbn_native_receiver.elf: $(NATIVE_RECEIVER_OBJS) build/native_receiver_linkfile
	$(EE_CC) -Tbuild/native_receiver_linkfile $(EE_OPTFLAGS) -o $@ $(NATIVE_RECEIVER_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 -Wl,--defsym,_heap_size=0x100000 $(EE_LIBS)
src/native_receiver.o: src/native_probe.c include/native_request.h include/native_launch.h include/mmce_catalog.h include/iso_scan.h build/native-runtime/runtime.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Wno-unused-function -Ibuild/native-runtime -DPSBBN_NATIVE_RECEIVER -c $< -o $@

src/combined_boot_main.o: src/real_image.c src/boot_launch.h src/mmce_scan.h src/collection_sync.h include/collection_io.h include/mmce_directory.h include/iso_scan.h include/mmce_catalog.h include/boot_elf.h include/boot_trace.h include/block_rpc.h include/real_checks.h include/suite_rpc.h vendor/common/fhi_fileid_config.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ivendor/common -DPSBBN_BOOT_BUILD -DPSBBN_COMBINED_BUILD -DPSBBN_FAST_BOOT -DPSBBN_PERF_TRACE -c $< -o $@

src/sound_boot_main.o: src/real_image.c src/boot_launch.h src/sound_start.h src/mmce_scan.h src/collection_sync.h include/iso_scan.h include/mmce_catalog.h include/boot_elf.h include/boot_trace.h include/block_rpc.h include/real_checks.h include/suite_rpc.h vendor/common/fhi_fileid_config.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ivendor/common -DPSBBN_BOOT_BUILD -DPSBBN_SOUND_BUILD -c $< -o $@

build/sound_linux_kernel.c: build/linux-sound/vmlinux
	$(PS2SDK)/bin/bin2c $< $@ linux_kernel

src/boot_main.o: src/real_image.c src/boot_launch.h include/boot_elf.h include/boot_trace.h include/block_rpc.h include/real_checks.h include/suite_rpc.h vendor/common/fhi_fileid_config.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ivendor/common -DPSBBN_BOOT_BUILD -c $< -o $@

build/boot_linkfile: $(PS2SDK)/ee/startup/linkfile
	mkdir -p build
	sed 's/0x00100000/0x01000000/' $< > $@

build/linux_kernel.c: build/linux/vmlinux
	$(PS2SDK)/bin/bin2c $< $@ linux_kernel

build/linux_sbios.c: vendor/tge/sbios/sbios_new.bin
	$(PS2SDK)/bin/bin2c $< $@ linux_sbios

vendor/tge/sbios/sbios_new.bin: $(wildcard vendor/tge/sbios/*.c vendor/tge/sbios/*.h vendor/tge/sbios/*.S vendor/tge/include/*) vendor/tge/sbios/Makefile vendor/tge/config.mk
	$(MAKE) -C vendor/tge/sbios ROM_MODULE_VERSION=new EE_CC=$(EE_CC) EE_OBJCOPY=$(EE_OBJCOPY)

build/intrelay.irx: $(wildcard vendor/intrelay/src/*) vendor/intrelay/Makefile.preflight
	$(MAKE) -C vendor/intrelay -f Makefile.preflight

build/sdrdrv.irx: $(wildcard vendor/sdrdrv/src/*) vendor/sdrdrv/Makefile.preflight
	$(MAKE) -C vendor/sdrdrv -f Makefile.preflight

build/linux_mcman.c: $(PS2SDK)/iop/irx/mcman-2000.irx
	$(PS2SDK)/bin/bin2c $< $@ linux_mcman_irx
build/linux_mcserv.c: $(PS2SDK)/iop/irx/mcserv-2000.irx
	$(PS2SDK)/bin/bin2c $< $@ linux_mcserv_irx
build/linux_padman.c: $(PS2SDK)/iop/irx/padman-2000.irx
	$(PS2SDK)/bin/bin2c $< $@ linux_padman_irx

psbbn_real.elf: $(REAL_OBJS)
	$(EE_CC) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $@ $(REAL_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) $(EE_LIBS)

src/real_image.o: include/real_checks.h include/suite_rpc.h vendor/common/fhi_fileid_config.h
src/real_image.o: EE_INCS += -Ivendor/common

build/atad_ro.irx: $(wildcard vendor/atad/src/* vendor/atad/include/* vendor/common/*) vendor/atad/Makefile
	$(MAKE) -C vendor/atad IOP_BIN=../../build/atad_ro.irx IOP_OBJS_DIR=../../build/atad_ro/ ATAD_EXTRA_CFLAGS=-DPSBBN_READ_ONLY

build/apa_ro.irx: $(wildcard vendor/apa/src/* vendor/libapa/src/* vendor/libapa/include/*) vendor/apa/Makefile.preflight
	$(MAKE) -C vendor/apa -f Makefile.preflight

build/pfs_ro.irx: $(wildcard vendor/pfs/src/* vendor/libpfs/src/* vendor/libpfs/include/*) vendor/pfs/Makefile.preflight
	$(MAKE) -C vendor/pfs -f Makefile.preflight

build/collection_ata.irx: iop/collection_ata.c iop/collection_imports.lst iop/collection_imports.h iop/Makefile.collection include/collection_txn.h include/collection_io.h
	$(MAKE) -C iop -f Makefile.collection
build/mmceman_scan.irx: $(wildcard vendor/mmceman_scan/src/* vendor/mmceman_scan/include/*) vendor/mmceman_scan/Makefile include/mmce_directory.h
	$(MAKE) -C vendor/mmceman_scan
build/apa_sync.irx: $(wildcard vendor/apa/src/* vendor/libapa/src/* vendor/libapa/include/*) vendor/apa/Makefile.collection
	$(MAKE) -C vendor/apa -f Makefile.collection
build/pfs_sync.irx: $(wildcard vendor/pfs/src/* vendor/libpfs/src/* vendor/libpfs/include/*) vendor/pfs/Makefile.collection
	$(MAKE) -C vendor/pfs -f Makefile.collection
build/collection_assets.c: Makefile.release
	$(MAKE) -f Makefile.release prepare

build/block_rpc.irx: iop/block.c iop/block_imports.lst iop/block_imports.h iop/Makefile.block include/block_rpc.h
	$(MAKE) -C iop -f Makefile.block BLOCK_EXTRA_CFLAGS=-DPSBBN_BLOCK_FHI_STATS

build/atad_checked.irx: $(wildcard vendor/atad/src/* vendor/atad/include/* vendor/common/*) vendor/atad/Makefile
	$(MAKE) -C vendor/atad IOP_BIN=../../build/atad_checked.irx IOP_OBJS_DIR=../../build/atad_checked/ ATAD_EXTRA_CFLAGS=-DPSBBN_CHECKED_IO

psbbn_suite.elf: $(SUITE_OBJS)
	$(EE_CC) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $@ $(SUITE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) $(EE_LIBS)

src/suite.o: include/suite_rpc.h include/suite_logic.h vendor/common/fhi_fileid_config.h
src/suite.o: EE_INCS += -Ivendor/common

build/mmcefhi.irx: $(wildcard vendor/mmcefhi/src/* vendor/mmcefhi/include/* vendor/common/*) vendor/mmcefhi/Makefile include/mmce_io_stats.h
	$(MAKE) -C vendor/mmcefhi

build/atad_emu.irx: $(wildcard vendor/atad/src/* vendor/atad/include/* vendor/common/*) vendor/atad/Makefile
	$(MAKE) -C vendor/atad

build/suite_rpc.irx: iop/suite.c iop/suite_imports.lst iop/suite_imports.h iop/Makefile.suite include/suite_rpc.h include/suite_logic.h
	$(MAKE) -C iop -f Makefile.suite

build/%.c: build/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

src/ata_probe.o: include/probe_rpc.h

build/ata_read_rpc.irx: iop/main.c iop/imports.lst iop/irx_imports.h iop/Makefile include/probe_rpc.h
	$(MAKE) -C iop

build/ata_read_rpc.c: build/ata_read_rpc.irx
	$(PS2SDK)/bin/bin2c $< $@ ata_read_rpc_irx

build/%.c: $(PS2SDK)/iop/irx/%.irx
	mkdir -p build
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

clean:
	rm -f $(EE_BIN) psbbn_suite.elf psbbn_real.elf psbbn_boot.elf psbbn_mmce_soundtest.elf $(EE_OBJS) $(SUITE_OBJS) $(REAL_OBJS) $(BOOT_OBJS) $(SOUND_BOOT_OBJS) build/*.c build/*.irx build/*.elf build/iop/* build/mmcefhi/* build/atad/* build/atad_ro/* build/atad_checked/* build/apa_ro/* build/pfs_ro/* build/suite_rpc/* build/block_rpc/* build/intrelay/* build/sdrdrv/*
