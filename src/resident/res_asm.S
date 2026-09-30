/*
 * MegaMem resident driver - clean-room reimplementation (no Datel code).
 * Linked at 0x000C0000. Header first, so the installer finds the entry points.
 */
    .set noreorder
    .set noat
    .section .text.start, "ax"
    .globl res_hdr
res_hdr:
    .word 0x524D4D58            /* "XMMR" */
    .word 2                     /* version */
    .word res_tramp             /* kernel-mode hook, jumped to from ExecPS2 */
    .word res_tramp_tail        /* 8 slots receiving the original ExecPS2 tail (mfc0..eret) */
    .word res_setreg            /* user-mode replacement for the program's SifSetReg stub */
    .word res_iop_patch         /* kernel function installed as syscall RES_SYSCALL */
    .word res_status            /* status/counters (u32[8]) */
    .word _res_end              /* end of blob (incl. bss) */
    .word res_k_setreg          /* v2: kernel-level SifSetReg hook (syscall 0x79 table entry) */
    .word res_orig_setreg       /* v2: u32 holding the original syscall 0x79 handler */

    .text
/* ---- ExecPS2 hook (kernel mode, EXL set, new program already loaded) ---- */
    .globl res_tramp
res_tramp:
    lui     $k0, %hi(res_regs)
    addiu   $k0, $k0, %lo(res_regs)
    sq      $1, 0x00($k0)
    sq      $2, 0x10($k0)
    sq      $3, 0x20($k0)
    sq      $4, 0x30($k0)
    sq      $5, 0x40($k0)
    sq      $6, 0x50($k0)
    sq      $7, 0x60($k0)
    sq      $8, 0x70($k0)
    sq      $9, 0x80($k0)
    sq      $10, 0x90($k0)
    sq      $11, 0xA0($k0)
    sq      $12, 0xB0($k0)
    sq      $13, 0xC0($k0)
    sq      $14, 0xD0($k0)
    sq      $15, 0xE0($k0)
    sq      $24, 0xF0($k0)
    sq      $25, 0x100($k0)
    sq      $28, 0x110($k0)
    sq      $29, 0x120($k0)
    sq      $31, 0x130($k0)
    sq      $16, 0x140($k0)
    sq      $17, 0x150($k0)
    sq      $18, 0x160($k0)
    sq      $19, 0x170($k0)
    sq      $20, 0x180($k0)
    sq      $21, 0x190($k0)
    sq      $22, 0x1A0($k0)
    sq      $23, 0x1B0($k0)
    sq      $30, 0x1C0($k0)
    lui     $sp, %hi(res_stack_top)
    addiu   $sp, $sp, %lo(res_stack_top)
    jal     res_scan_program
    nop
    lui     $k0, %hi(res_regs)
    addiu   $k0, $k0, %lo(res_regs)
    lq      $1, 0x00($k0)
    lq      $2, 0x10($k0)
    lq      $3, 0x20($k0)
    lq      $4, 0x30($k0)
    lq      $5, 0x40($k0)
    lq      $6, 0x50($k0)
    lq      $7, 0x60($k0)
    lq      $8, 0x70($k0)
    lq      $9, 0x80($k0)
    lq      $10, 0x90($k0)
    lq      $11, 0xA0($k0)
    lq      $12, 0xB0($k0)
    lq      $13, 0xC0($k0)
    lq      $14, 0xD0($k0)
    lq      $15, 0xE0($k0)
    lq      $24, 0xF0($k0)
    lq      $25, 0x100($k0)
    lq      $28, 0x110($k0)
    lq      $29, 0x120($k0)
    lq      $31, 0x130($k0)
    lq      $16, 0x140($k0)
    lq      $17, 0x150($k0)
    lq      $18, 0x160($k0)
    lq      $19, 0x170($k0)
    lq      $20, 0x180($k0)
    lq      $21, 0x190($k0)
    lq      $22, 0x1A0($k0)
    lq      $23, 0x1B0($k0)
    lq      $30, 0x1C0($k0)
    .globl res_tramp_tail
res_tramp_tail:                 /* filled by the installer with the displaced kernel code */
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    eret                        /* safety net (never reached if the tail ends with eret) */
    nop

/* ---- user-mode SifSetReg replacement ---- */
    .globl res_setreg
res_setreg:
    addiu   $sp, $sp, -32
    sd      $ra, 0($sp)
    sd      $a0, 8($sp)
    sd      $a1, 16($sp)
    addiu   $v1, $zero, 0x79    /* real SifSetReg syscall */
    syscall
    nop
    sd      $v0, 24($sp)
    ld      $a0, 8($sp)
    jal     res_on_setreg
    ld      $a1, 16($sp)
    ld      $v0, 24($sp)
    ld      $ra, 0($sp)
    jr      $ra
    addiu   $sp, $sp, 32

    .globl res_call_patch
res_call_patch:
    addiu   $v1, $zero, 252     /* RES_SYSCALL */
    syscall
    nop
    jr      $ra
    nop
