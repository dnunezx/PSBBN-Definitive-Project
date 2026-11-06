/* SPDX-License-Identifier: MIT
 * EE handoff utilities: execute from KSEG0 before discarding user mappings.
 */
.set noreorder
.text
.globl psbbn_enter_linux
.type psbbn_enter_linux, @function
.ent psbbn_enter_linux
psbbn_enter_linux:
    /* C may hoist KSEG loads above an inline mode switch. No memory access
     * is permitted here until Status has selected kernel mode with IE off.
     * Preserve a0 (the IOP address) and enter a separate, non-inlined C body.
     */
    di
    sync.p
    lui $t0, 0x3001
    mtc0 $t0, $12
    sync.p
    j psbbn_enter_linux_privileged
    nop
.end psbbn_enter_linux
.size psbbn_enter_linux, .-psbbn_enter_linux

.globl psbbn_exception_entry
psbbn_exception_entry:
    mfc0 $a0, $13
    mfc0 $a1, $14
    mfc0 $a2, $8
    mfc0 $a3, $12
    lui $sp, 0x81f8
    addiu $sp, $sp, -4096
    j psbbn_handoff_fault
    nop

.globl psbbn_jump_kseg
psbbn_jump_kseg:
    lui $t0, 0x8000
    or $ra, $ra, $t0
    or $sp, $sp, $t0
    or $gp, $gp, $t0
    jr $ra
    nop

.globl psbbn_flush_caches
.align 6
psbbn_flush_caches:
    lui $t0, 0x8000
    li $t1, 64
1:
    sync.l
    cache 0x14, 0($t0)
    sync.l
    cache 0x14, 1($t0)
    addiu $t0, $t0, 64
    addiu $t1, $t1, -1
    bnez $t1, 1b
    nop
    sync.l
    lui $t0, 0x8000
    li $t1, 128
2:
    sync.p
    cache 0x07, 0($t0)
    sync.p
    cache 0x07, 1($t0)
    addiu $t0, $t0, 64
    addiu $t1, $t1, -1
    bnez $t1, 2b
    nop
    sync.p
    jr $ra
    nop

.globl psbbn_flush_tlbs
psbbn_flush_tlbs:
    mtc0 $zero, $6
    mtc0 $zero, $5
    mtc0 $zero, $2
    mtc0 $zero, $3
    move $t0, $zero
    lui $t1, 0x8000
3:
    mtc0 $t0, $0
    mtc0 $t1, $10
    sync.p
    tlbwi
    sync.p
    addiu $t0, $t0, 1
    addiu $t1, $t1, 0x2000
    slti $t2, $t0, 48
    bnez $t2, 3b
    nop
    jr $ra
    nop
