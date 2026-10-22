.syntax unified
.cpu cortex-m0plus
.thumb

.section .vectors, "a", %progbits
.global microflow_vectors
microflow_vectors:
  .word __stack_top
  .word microflow_reset_handler
  .rept 46
  .word microflow_default_handler
  .endr

.section .text.microflow_reset_handler, "ax", %progbits
.thumb_func
.global microflow_reset_handler
.type microflow_reset_handler, %function
microflow_reset_handler:
  ldr r0, =__data_start
  ldr r1, =__data_end
  ldr r2, =__data_load
1:
  cmp r0, r1
  bcs 2f
  ldr r3, [r2]
  adds r2, #4
  str r3, [r0]
  adds r0, #4
  b 1b
2:
  ldr r0, =__bss_start
  ldr r1, =__bss_end
  movs r2, #0
3:
  cmp r0, r1
  bcs 4f
  str r2, [r0]
  adds r0, #4
  b 3b
4:
  bl main
5:
  b 5b

.thumb_func
.global microflow_default_handler
.type microflow_default_handler, %function
microflow_default_handler:
  b microflow_default_handler
