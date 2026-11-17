/* SPDX-License-Identifier: MIT
 * A raw MBR payload enters at 1 MiB; preserve a0 for the SDK startup ABI.
 */
.set noreorder
.section .text.native_receiver_entry,"ax",@progbits
.globl native_receiver_entry
native_receiver_entry:
    lui $8, 0x2103
    ori $8, $8, 0xd000
    li $9, 3
    sw $9, 4($8)
    j __start
    nop
