/* Bare-metal start for QEMU "virt": one hart, M-mode, entered at 0x80000000 with -bios none. */
    .section .text.start, "ax"
    .globl _start
_start:
    csrr    t0, mhartid
    bnez    t0, park
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, _stack_top
    la      t0, trap
    csrw    mtvec, t0
    li      t0, (1 << 13)           /* mstatus.FS = initial: the harness must not trap on FP */
    csrs    mstatus, t0
    call    tls_init                /* sets tp; stb's failure reason and flip flags are __thread */
    call    main_c
    j       finish

trap:
    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    la      sp, _stack_top
    call    on_trap
park:
    wfi
    j       park
