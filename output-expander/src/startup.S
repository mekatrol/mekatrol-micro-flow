
    # CH32V003F4P6 startup and interrupt entry.
    #
    # Target: QingKe V2A (RV32EC)
    # ABI:    ilp32e
    #
    # The firmware uses project-owned startup code rather than
    # the WCH runtime. Hardware interrupt register stacking is
    # disabled; the USART1 interrupt wrapper preserves registers.

    # ---------------------------------------------------------
    # Reset entry and interrupt vector table
    # ---------------------------------------------------------

    # After reset, execution begins at the start of Flash.
    #
    # QingKe vector mode 11 uses a table containing absolute
    # handler addresses. Each entry occupies four bytes.
    #
    # The vector table is aligned to 1024 bytes as required
    # by the QingKe V2A interrupt controller.
    .section .init, "ax", @progbits
    .globl _start
    .type _start, @function
    .balign 1024

_start:
    # The first vector slot contains the reset jump.
    #
    # Disable compressed instructions for this instruction
    # so it occupies exactly four bytes. Otherwise, a
    # compressed two-byte jump would shift vector offsets.
    .option push
    .option norvc
    j reset_handler
    .option pop

    # Interrupt vector slots 1 through 16.
    #
    # These interrupts are not currently supported.
    # Point them to a default handler so an unexpected
    # interrupt stops in a known location.
    .rept 16
    .word default_irq_handler
    .endr

    # PVD is interrupt number 17.
    .word pvd_irq_entry

    # Interrupt vector slots 18 through 31.
    .rept 14
    .word default_irq_handler
    .endr

    # USART1 is interrupt number 32.
    #
    # Offset = 32 * 4 = 0x80 bytes from the table base.
    #
    # This entry contains the address of the assembly
    # wrapper, not the C interrupt function directly.
    .word usart1_irq_entry

    .size _start, . - _start


    # ---------------------------------------------------------
    # Reset handler
    # ---------------------------------------------------------

    .section .text.reset_handler, "ax", @progbits
    .globl reset_handler
    .type reset_handler, @function
    .balign 4

reset_handler:
    # GCC may address small global objects relative to gp.
    #
    # Disable linker relaxation around this load so gp
    # receives the exact linker-defined address before
    # any generated C code executes.
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    # RISC-V stacks grow downward.
    #
    # Start at the address immediately above the MCU's
    # 2 KiB SRAM region, as defined by the linker script.
    la sp, __stack_top

    # ---------------------------------------------------------
    # Initialize .data
    # ---------------------------------------------------------

    # Initialized global and static C variables have their
    # initial values stored in Flash.
    #
    # Copy these values into SRAM before calling main(),
    # because the application expects them to be writable.
    #
    # The linker provides the load address, destination
    # start address and destination end address.
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
    # ---------------------------------------------------------
    # Initialize .bss
    # ---------------------------------------------------------

    # The C language requires uninitialized global and
    # static objects to contain zero when main() begins.
    #
    # Clear the entire .bss section before the application
    # starts. The linker keeps these boundaries word-aligned.
    la a0, __bss_start
    la a1, __bss_end

3:
    bgeu a0, a1, 4f

    sw zero, 0(a0)
    addi a0, a0, 4

    j 3b

4:
    # ---------------------------------------------------------
    # Configure CPU interrupt handling
    # ---------------------------------------------------------

    # Disable global machine interrupts during startup.
    #
    # USART and queue initialization must complete before
    # an interrupt is allowed to execute the C handler.
    #
    # The main application will enable interrupts later,
    # after USART1 and its receive queue are ready.
    csrci mstatus, 8

    # QingKe interrupt system configuration register.
    #
    # CSR 0x804 controls hardware stacking and interrupt
    # nesting on the QingKe V2A processor.
    #
    # Clear the register to disable hardware stacking
    # and nesting. Our assembly wrapper is responsible
    # for preserving interrupted CPU registers.
    #
    # This avoids depending on WCH-specific interrupt
    # compiler attributes.
    csrw 0x804, zero

    # Configure the interrupt vector table.
    #
    # mtvec contains the vector table base address and
    # the two-bit interrupt addressing mode.
    #
    # Mode 11:
    #   Bit 0 = 1: vectored interrupt addressing.
    #   Bit 1 = 1: table contains absolute addresses.
    #
    # The table base is 1024-byte aligned, so the two
    # low bits can safely encode the addressing mode.
    la t0, _start
    ori t0, t0, 3
    csrw mtvec, t0

    # ---------------------------------------------------------
    # Enter main application
    # ---------------------------------------------------------

    # Global interrupts remain disabled.
    #
    # main() initializes the board, RX queue and USART
    # before explicitly enabling reception interrupts.
    call main

    # main() is not expected to return.
    #
    # If it does, remain in a known loop rather than
    # executing unrelated memory as instructions.
5:
    j 5b

    .size reset_handler, . - reset_handler


    # ---------------------------------------------------------
    # Default interrupt handler
    # ---------------------------------------------------------

    .section .text.default_irq_handler, "ax", @progbits
    .globl default_irq_handler
    .type default_irq_handler, @function
    .balign 4

default_irq_handler:
    # An unsupported interrupt indicates an unexpected
    # peripheral or processor condition.
    #
    # Remain here for debugger inspection rather than
    # returning to a potentially repeating interrupt.
1:
    j 1b

    .size default_irq_handler, . - default_irq_handler


    # ---------------------------------------------------------
    # USART1 assembly interrupt wrapper
    # ---------------------------------------------------------

    .section .text.usart1_irq_entry, "ax", @progbits
    .globl usart1_irq_entry
    .type usart1_irq_entry, @function
    .balign 4

usart1_irq_entry:
    # The CH32V003 implements RV32E, which exposes
    # registers x0 through x15.
    #
    # The interrupt may occur at any instruction,
    # including while the foreground C code is using
    # registers that a normal function call may alter.
    #
    # Save all writable RV32E registers except sp,
    # whose original value is restored by reversing
    # the stack allocation.
    #
    # A 64-byte frame provides room for 14 registers
    # (56 bytes) while maintaining stack alignment.

    addi sp, sp, -64

    # Save return address and global registers.
    sw ra,  0(sp)
    sw gp,  4(sp)
    sw tp,  8(sp)

    # Save temporary registers.
    sw t0, 12(sp)
    sw t1, 16(sp)
    sw t2, 20(sp)

    # Save callee-saved registers.
    sw s0, 24(sp)
    sw s1, 28(sp)

    # Save argument and return-value registers.
    sw a0, 32(sp)
    sw a1, 36(sp)
    sw a2, 40(sp)
    sw a3, 44(sp)
    sw a4, 48(sp)
    sw a5, 52(sp)

    # Call the ordinary C USART1 interrupt handler.
    #
    # The C handler reads USART status and data,
    # then pushes a byte or error event into the
    # interrupt-to-foreground receive queue.
    #
    # It must not block, parse frames or transmit.
    call usart1_irq_handler

    # Restore the interrupted register values.
    #
    # Reverse the save order so the CPU returns to
    # exactly the general-register state it had
    # before entering the interrupt handler.
    lw a5, 52(sp)
    lw a4, 48(sp)
    lw a3, 44(sp)
    lw a2, 40(sp)
    lw a1, 36(sp)
    lw a0, 32(sp)

    lw s1, 28(sp)
    lw s0, 24(sp)

    lw t2, 20(sp)
    lw t1, 16(sp)
    lw t0, 12(sp)

    lw tp,  8(sp)
    lw gp,  4(sp)
    lw ra,  0(sp)

    # Restore the original stack pointer.
    addi sp, sp, 64

    # Return from the machine interrupt.
    #
    # mret restores execution at mepc and restores
    # the previous machine interrupt-enable state.
    #
    # A normal ret would be incorrect here because
    # interrupt entry is not an ordinary function call.
    mret

    .size usart1_irq_entry, . - usart1_irq_entry


    # ---------------------------------------------------------
    # PVD assembly interrupt wrapper
    # ---------------------------------------------------------

    .section .text.pvd_irq_entry, "ax", @progbits
    .globl pvd_irq_entry
    .type pvd_irq_entry, @function
    .balign 4

pvd_irq_entry:
    # Preserve every writable RV32E register because an interrupt may arrive
    # at any instruction and the C handler is an ordinary function.
    addi sp, sp, -64
    sw ra,  0(sp)
    sw gp,  4(sp)
    sw tp,  8(sp)
    sw t0, 12(sp)
    sw t1, 16(sp)
    sw t2, 20(sp)
    sw s0, 24(sp)
    sw s1, 28(sp)
    sw a0, 32(sp)
    sw a1, 36(sp)
    sw a2, 40(sp)
    sw a3, 44(sp)
    sw a4, 48(sp)
    sw a5, 52(sp)

    call pvd_irq_handler

    lw a5, 52(sp)
    lw a4, 48(sp)
    lw a3, 44(sp)
    lw a2, 40(sp)
    lw a1, 36(sp)
    lw a0, 32(sp)
    lw s1, 28(sp)
    lw s0, 24(sp)
    lw t2, 20(sp)
    lw t1, 16(sp)
    lw t0, 12(sp)
    lw tp,  8(sp)
    lw gp,  4(sp)
    lw ra,  0(sp)
    addi sp, sp, 64
    mret

    .size pvd_irq_entry, . - pvd_irq_entry
