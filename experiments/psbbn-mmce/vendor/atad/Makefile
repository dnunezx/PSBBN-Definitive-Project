IOP_BIN = ../../build/atad_emu.irx
IOP_OBJS = ps2atad.o imports.o exports.o
IOP_SRC_DIR = src/
IOP_INC_DIR = include/
IOP_OBJS_DIR = ../../build/atad/
IOP_CFLAGS = -I../common -Isrc
.DEFAULT_GOAL := all
all: $(IOP_BIN)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
# Neutrino's ATAD definitions must precede PS2SDK's compatibility aliases.
IOP_CFLAGS := -Iinclude $(IOP_CFLAGS)
