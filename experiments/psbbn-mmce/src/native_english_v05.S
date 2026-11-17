/* SPDX-License-Identifier: MIT
 * Isolated addition to exact v05: replaces only its final FlushCache(2) call.
 * Keep that syscall first. Use the SDK's legacy-vs-current language rule.
 * No new loader, storage, timing, video, audio, timezone or version changes.
 */
.set noreorder
.set noat
.text
.globl native_english_v05
native_english_v05:
    addiu $sp, $sp, -16
    li $v1, 100                 /* Original FlushCache(2), a0 remains 2. */
    syscall
    move $a0, $sp
    li $v1, 75                  /* GetOsdConfigParam(&temporary). */
    syscall
    lw $t0, 0($sp)
    andi $t1, $t0, 0xe000       /* ConfigParam.version. */
    bnez $t1, modern_language
    nop
    ori $t0, $t0, 0x10          /* Version0: legacy Japanese/English bit ONLY. */
    b write_language
    nop
modern_language:
    lui $t1, 0xffe0
    ori $t1, $t1, 0xffff        /* Version1/2: clear only bits16..20. */
    and $t0, $t0, $t1
    lui $t1, 1
    or $t0, $t0, $t1            /* Standard English=1. */
write_language:
    sw $t0, 0($sp)
    li $v1, 74                  /* SetOsdConfigParam(&temporary). */
    syscall
    jr $ra
    addiu $sp, $sp, 16
