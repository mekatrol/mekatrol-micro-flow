ENTRY(microflow_reset_handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2M
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS
{
  .boot2 ORIGIN(FLASH) :
  {
    KEEP(*(.boot2))
  } > FLASH

  .vectors ORIGIN(FLASH) + 0x100 :
  {
    KEEP(*(.vectors))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .data :
  {
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM AT> FLASH
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM

  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  /DISCARD/ : { *(.comment*) *(.note*) }
}
