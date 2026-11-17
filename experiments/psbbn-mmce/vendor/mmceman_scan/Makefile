# Full filesystem driver used before Linux; preserve the final FHI transport.
IOP_BIN = ../../build/mmceman_scan.irx
IOP_OBJS = ioplib.o sio2man_hook.o mmce_sio2.o mmce_cmds.o mmce_fs.o imports.o mmceman.o
IOP_SRC_DIR = src/
IOP_INC_DIR = include/
IOP_OBJS_DIR = ../../build/mmceman_scan/
IOP_INCS += -I../../include
IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0
.DEFAULT_GOAL := all
all: $(IOP_BIN)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
$(IOP_OBJS_DIR)mmce_fs.o: ../../include/mmce_directory.h
