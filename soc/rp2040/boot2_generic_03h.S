/* Derived from Raspberry Pi pico-sdk 2.2.0 boot2_generic_03h.S. */
/* SPDX-License-Identifier: BSD-3-Clause */
.syntax unified
.cpu cortex-m0plus
.thumb

.equ XIP_SSI_BASE, 0x18000000
.equ XIP_BASE, 0x10000000
.equ PPB_BASE, 0xe0000000
.equ VTOR_OFFSET, 0xed08

.section .text, "ax", %progbits
.global _stage2_boot
.type _stage2_boot, %function
.thumb_func
_stage2_boot:
  push {lr}
  ldr r3, =XIP_SSI_BASE
  movs r1, #0
  str r1, [r3, #0x08]
  movs r1, #4
  str r1, [r3, #0x14]
  ldr r1, =0x001f0300
  str r1, [r3, #0x00]
  ldr r1, =0x03000218
  ldr r0, =(XIP_SSI_BASE + 0xf4)
  str r1, [r0]
  movs r1, #0
  str r1, [r3, #0x04]
  movs r1, #1
  str r1, [r3, #0x08]
  pop {r0}
  cmp r0, #0
  bne 1f
  ldr r0, =(XIP_BASE + 0x100)
  ldr r1, =(PPB_BASE + VTOR_OFFSET)
  str r0, [r1]
  ldmia r0, {r0, r1}
  msr msp, r0
  bx r1
1:
  bx r0
  .ltorg
