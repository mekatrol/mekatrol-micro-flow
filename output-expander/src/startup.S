    # The CH32V003 fetches its first instruction from address zero after reset.
    # Keep this dedicated section first in Flash and immediately branch to the
    # full reset routine, leaving application startup independent of a runtime.
    .section .init, "ax", @progbits
    .globl _start
    .type _start, @function
    .align 2
_start:
    j reset_handler

    .section .text.reset_handler, "ax", @progbits
    .globl reset_handler
    .type reset_handler, @function
reset_handler:
    # GCC may address small global objects relative to gp. Disable linker
    # relaxation around this load so gp receives the exact linker-defined
    # address before any generated C code can use it.
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # RISC-V stacks grow downward, so begin at the byte immediately above the
    # CH32V003's 2 KiB SRAM region.
    la sp, __stack_top

    # C objects with initial values live in Flash in the firmware image but
    # must be writable in SRAM. Copy the complete .data load image one word at
    # a time before main can observe any static variable.
    la a0, __data_load
    la a1, __data_start
    la a2, __data_end
1:
    bgeu a1, a2, 2f
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j 1b
2:
    # The C language requires every uninitialized static object to start at
    # zero. Clear the linker-defined .bss range before entering main.
    la a0, __bss_start
    la a1, __bss_end
3:
    bgeu a0, a1, 4f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 3b
4:
    # main owns the application forever. If it ever returns unexpectedly,
    # remain in a bounded loop rather than executing bytes beyond the image.
    call main
5:
    j 5b
    .size reset_handler, . - reset_handler
