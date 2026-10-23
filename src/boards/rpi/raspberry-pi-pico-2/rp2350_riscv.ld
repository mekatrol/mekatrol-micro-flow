ENTRY(_start)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 4M
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS
{
  .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .data : { *(.data*) . = ALIGN(4); } > RAM AT> FLASH
  .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(4); } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  /DISCARD/ : { *(.comment*) *(.note*) }
}
