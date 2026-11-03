EE_BIN = ata_probe.elf
EE_OBJS = src/ata_probe.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman.o build/ata_read_rpc.o
EE_LIBS = -ldebug -lfileXio -lpatches -lkernel
EE_CFLAGS = -Wextra -Werror
EE_INCS = -Iinclude
SUITE_OBJS = src/suite.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman.o build/mmcefhi.o build/atad_emu.o build/suite_rpc.o build/bdm.o build/bdmfs_fatfs.o build/usbd.o build/usbmass_bd.o
.DEFAULT_GOAL := all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

.PHONY: all clean
all: $(EE_BIN) psbbn_suite.elf

psbbn_suite.elf: $(SUITE_OBJS)
	$(EE_CC) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $@ $(SUITE_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) $(EE_LIBS)

src/suite.o: include/suite_rpc.h include/suite_logic.h vendor/common/fhi_fileid_config.h
src/suite.o: EE_INCS += -Ivendor/common

build/mmcefhi.irx: $(wildcard vendor/mmcefhi/src/* vendor/mmcefhi/include/* vendor/common/*) vendor/mmcefhi/Makefile
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
	rm -f $(EE_BIN) psbbn_suite.elf $(EE_OBJS) $(SUITE_OBJS) build/*.c build/*.irx build/*.elf build/iop/* build/mmcefhi/* build/atad/* build/suite_rpc/*
