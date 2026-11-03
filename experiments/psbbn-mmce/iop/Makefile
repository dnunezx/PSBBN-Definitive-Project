IOP_BIN = ../build/ata_read_rpc.irx
IOP_OBJS = main.o imports.o
IOP_OBJS_DIR = ../build/iop/
IOP_CFLAGS = -I../include -Wextra -Werror
.DEFAULT_GOAL := all
all: $(IOP_BIN)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal

$(IOP_OBJS_DIR)main.o: ../include/probe_rpc.h
$(IOP_OBJS_DIR)imports.o: irx_imports.h
