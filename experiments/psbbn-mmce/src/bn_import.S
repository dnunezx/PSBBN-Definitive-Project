/* Linux o32 PIC import adapter for the exact supported dashboard GOT. */
.set noreorder
.text
.globl bn_import3
bn_import3:
    lw $25,0($4)
    move $4,$5
    move $5,$6
    move $6,$7
    lui $28,0x100d
    ori $28,$28,0x9910
    jr $25
    nop
