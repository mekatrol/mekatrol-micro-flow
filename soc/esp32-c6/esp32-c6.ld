ENTRY(_start)

MEMORY
{
  SRAM (rwx) : ORIGIN = 0x40800000, LENGTH = 0x6E000
}

SECTIONS
{
  .text :
  {
    KEEP(*(.text.start))
    *(.text*)
    *(.rodata*)
    . = ALIGN(16);
  } > SRAM

  .data :
  {
    *(.data*)
    . = ALIGN(16);
  } > SRAM

  .bss (NOLOAD) :
  {
    *(.bss*)
    *(COMMON)
    . = ALIGN(16);
  } > SRAM

  __stack_top = ORIGIN(SRAM) + LENGTH(SRAM) - 16;
  /DISCARD/ : { *(.comment*) *(.note*) }
}
