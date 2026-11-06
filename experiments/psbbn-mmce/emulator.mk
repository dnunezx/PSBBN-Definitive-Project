# PCSX2-only build; reuses the exact current kernel, SBIOS and handoff.
EMU_OBJS = src/emulator_boot.o src/boot_handoff.o build/sio2man.o build/emulator/block_rpc.o build/intrelay.o build/linux_mcman.o build/linux_mcserv.o build/linux_padman.o build/linux_kernel.o build/linux_sbios.o build/emulator/native_dev9.o build/emulator/native_atad.o build/emulator/checks.o
src/emulator_boot.o: src/emulator_boot.c src/boot_launch.h include/real_checks.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
psbbn_emulator.elf: $(EMU_OBJS) build/boot_linkfile
	$(EE_CC) -Tbuild/boot_linkfile $(EE_OPTFLAGS) -o $@ $(EMU_OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,--defsym,_stack=0x01f80000 -Wl,--defsym,_stack_size=0x20000 $(EE_LIBS)
