.section .text.start, "ax", @progbits
.align 4
.global _start
.type _start, @function
_start:
  movi a1, __stack_top
  movi a0, 0
  call0 main
1:
  j 1b
