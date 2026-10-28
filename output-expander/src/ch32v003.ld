/* This image targets only the CH32V003F4P6: 16 KiB code Flash mapped at zero
 * and 2 KiB SRAM mapped at 0x20000000. Keeping these limits in the linker makes
 * an oversized image fail at build time instead of corrupting the device. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 16K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 2K
}

SECTIONS
{
    .init ORIGIN(FLASH) :
    {
        /* KEEP prevents dead-section removal from discarding the reset branch,
         * even though ordinary C code has no reference to _start. */
        KEEP(*(.init))
    } > FLASH

    .text :
    {
        /* Executable code and read-only constants both stay in Flash so the
         * very limited SRAM is reserved for mutable state and stack. */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .data : ALIGN(4)
    {
        /* Mutable initialized objects execute from SRAM. AT > FLASH gives the
         * section a separate load address consumed by startup.S. */
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        /* The RISC-V ABI places gp near small data. The project disables
         * compiler small-data generation, but defining it keeps startup ABI
         * complete and safe if tool-generated objects reference gp. */
        __global_pointer$ = . + 0x800;
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        /* NOLOAD avoids storing zero-filled bytes in Flash; startup.S clears
         * this range in SRAM instead. */
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* The stack begins at the top of SRAM and grows toward static data. The
     * assertions provide immediate diagnostics for missing startup code or
     * statically allocated data that already exceeds physical memory. */
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top, "static data exceeds SRAM")
    ASSERT(SIZEOF(.init) > 0, "reset vector is missing")
}
