OUTPUT_ARCH(mips)
ENTRY(bn_pack_entry)
SECTIONS {
  . = 0x00980000;
  .text : { *(.text*) *(.rodata*) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.pdr) *(.reginfo) *(.MIPS.abiflags) *(.gnu.attributes) }
}
ASSERT(SIZEOF(.text) <= 0x1000, "Resource hook exceeds its code page")
ASSERT(SIZEOF(.data) == 0, "Unexpected initialized hook data")
ASSERT(SIZEOF(.bss) == 0, "Unexpected hook globals")
