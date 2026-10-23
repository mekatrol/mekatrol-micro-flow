ENTRY(_start)

MEMORY
{
  IRAM (rx) : ORIGIN = 0x40378000, LENGTH = 0x53700
  DRAM (rw) : ORIGIN = 0x3FC88000, LENGTH = 0x53700
}

SECTIONS
{
  .text :
  {
    KEEP(*(.text.start))
    *(.literal*)
    *(.text*)
    *(.rodata*)
    . = ALIGN(16);
  } > IRAM

  .data :
  {
    *(.data*)
    . = ALIGN(16);
  } > DRAM

  .bss (NOLOAD) :
  {
    *(.bss*)
    *(COMMON)
    . = ALIGN(16);
  } > DRAM

  __stack_top = ORIGIN(DRAM) + LENGTH(DRAM) - 16;
  /DISCARD/ : { *(.comment*) *(.note*) }
}
