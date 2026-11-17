/* SPDX-License-Identifier: MIT
 * Candidate entry for the existing post-Linux kernel initialization patch.
 * Runs only after the ROM kernel has been initialized. No IOP or disk writes.
 * Arguments must outlive the receiver's BSS clear at 1..3 MiB.
 */
.set noreorder
.text
.globl native_restart_gate
native_restart_gate:
    lui $t4, 0x2103
    ori $t4, $t4, 0xd000
    lui $t5, 0x324c
    ori $t5, $t5, 0x4250
    sw $t5, 0($t4)
    li $t5, 1
    sw $t5, 4($t4)
    sw $zero, 8($t4)
    lui $t0, 0x2100
    lw $t1, 4($t0)             /* Original selected-title argument count. */
    lw $t2, 8($t0)             /* Preserved vector, not a dereferenced pointer. */
    sw $t1, 12($t4)
    sw $t2, 16($t4)
    sltiu $t3, $t1, 3
    bnez $t3, gate_bad_count
    nop
    sltiu $t3, $t1, 9
    beqz $t3, gate_bad_count
    nop
    lui $t3, 0x1fff
    ori $t3, $t3, 0xffff
    and $t2, $t2, $t3
    lui $t3, 0x0103
    ori $t3, $t3, 0xf000
    bne $t2, $t3, gate_bad_pointer
    nop
gate_arguments:
    /* PS2SDK SetupThread returns to application mode. Use its user-accessible
     * uncached mirror (physical | 0x20000000), including argv string pointers.
     * A KSEG1 address can fault before main when SDK startup dereferences it. */
    lui $t0, 0x2103
    ori $t0, $t0, 0xe000
    move $t2, $t0
    addiu $t3, $t0, 336
gate_clear:
    sw $zero, 0($t2)
    addiu $t2, $t2, 4
    bne $t2, $t3, gate_clear
    nop
    li $t2, 3
    sw $t2, 4($t0)             /* sargs_start.args.argc */
    addiu $t2, $t0, 80
    sw $t2, 8($t0)
    addiu $t2, $t0, 96
    sw $t2, 12($t0)
    addiu $t2, $t0, 100
    sw $t2, 16($t0)
    lui $t2, 0x306d
    ori $t2, $t2, 0x6f72       /* rom0 */
    sw $t2, 80($t0)
    lui $t2, 0x5242
    ori $t2, $t2, 0x4d3a       /* :MBR */
    sw $t2, 84($t0)
    lui $t2, 0x544f
    ori $t2, $t2, 0x4f42       /* BOOT */
    sw $t2, 88($t0)
    addiu $t1, $t1, 48
    sb $t1, 96($t0)
    lui $t2, 0x6633
    ori $t2, $t2, 0x3031       /* 103f */
    sw $t2, 100($t0)
    lui $t2, 0x0030
    ori $t2, $t2, 0x3030       /* 000 + NUL */
    sw $t2, 104($t0)
    sync
    li $t5, 2
    sw $t5, 4($t4)
    move $a0, $t0
    lui $t9, 0x0010
    jr $t9
    nop
gate_bad_count:
    li $t5, 0xe001
    sw $t5, 8($t4)
    move $t1, $zero
    b gate_arguments
    nop
gate_bad_pointer:
    li $t5, 0xe002
    sw $t5, 8($t4)
    move $t1, $zero
    b gate_arguments
    nop
