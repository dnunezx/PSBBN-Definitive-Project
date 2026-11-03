IOP_BIN = ../../build/mmcefhi.irx
IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o exports.o imports.o mmcedrv.o mmce_fhi.o
IOP_SRC_DIR = src/
IOP_INC_DIR = include/
IOP_OBJS_DIR = ../../build/mmcefhi/
IOP_CFLAGS = -DMMCEDRV -DFHI -I../common
.DEFAULT_GOAL := all
all: $(IOP_BIN)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
