EE_BIN = ata_probe.elf
EE_OBJS = src/ata_probe.o build/iomanX.o build/fileXio.o build/sio2man.o build/mmceman.o build/ata_read_rpc.o
EE_LIBS = -ldebug -lfileXio -lpatches -lkernel
EE_CFLAGS = -Wextra -Werror
EE_INCS = -Iinclude
.DEFAULT_GOAL := all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

.PHONY: all clean
all: $(EE_BIN)

src/ata_probe.o: include/probe_rpc.h

build/ata_read_rpc.irx: iop/main.c iop/imports.lst iop/irx_imports.h iop/Makefile include/probe_rpc.h
	$(MAKE) -C iop

build/ata_read_rpc.c: build/ata_read_rpc.irx
	$(PS2SDK)/bin/bin2c $< $@ ata_read_rpc_irx

build/%.c: $(PS2SDK)/iop/irx/%.irx
	mkdir -p build
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS) build/*.c build/*.irx build/*.elf build/iop/*
