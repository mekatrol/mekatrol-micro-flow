.section .text.start, "ax", @progbits
.global _start
.type _start, @function
_start:
  la sp, __stack_top
  call main
1:
  j 1b
