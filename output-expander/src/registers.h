#ifndef REGISTERS_H
#define REGISTERS_H

/* Peripheral registers are memory-mapped 32-bit values. Volatile prevents the
 * compiler from removing or combining accesses whose effects occur in
 * hardware rather than ordinary memory. */
#define REG32(address) (*(volatile uint32_t *)(address))

/* Reset and clock control registers used to turn the 24 MHz internal
 * oscillator into the 48 MHz system clock and enable the GPIO peripherals. */
#define RCC_CTLR REG32(UINT32_C(0x40021000))
#define RCC_CFGR0 REG32(UINT32_C(0x40021004))
#define RCC_APB2PCENR REG32(UINT32_C(0x40021018))

/* Flash needs one wait state when the CPU runs above 24 MHz; without it,
 * instruction reads can fail after switching the system clock to 48 MHz. */
#define FLASH_ACTLR REG32(UINT32_C(0x40022000))

/* Each CH32V003 GPIO pin uses four configuration bits. BSHR atomically drives
 * selected pins HIGH and BCR atomically drives them LOW, avoiding a read/write
 * race on the shared output-data register. */
#define GPIOC_CFGLR REG32(UINT32_C(0x40011000))
#define GPIOC_BSHR REG32(UINT32_C(0x40011010))
#define GPIOC_BCR REG32(UINT32_C(0x40011014))
#define GPIOD_CFGLR REG32(UINT32_C(0x40011400))
#define GPIOD_BSHR REG32(UINT32_C(0x40011410))
#define GPIOD_BCR REG32(UINT32_C(0x40011414))

/* QingKe SysTick is a free-running 32-bit counter. It is used only for elapsed
 * time here, so no compare interrupt or reload behavior is needed. */
#define SYSTICK_CTLR REG32(UINT32_C(0xE000F000))
#define SYSTICK_CNT REG32(UINT32_C(0xE000F008))

#define RCC_PLLON (UINT32_C(1) << 24)
#define RCC_PLLRDY (UINT32_C(1) << 25)
#define RCC_SW_PLL UINT32_C(2)
#define RCC_SWS_PLL (UINT32_C(2) << 2)
#define RCC_SWS_MASK (UINT32_C(3) << 2)
#define RCC_IOPCEN (UINT32_C(1) << 4)
#define RCC_IOPDEN (UINT32_C(1) << 5)
#define GPIO_OUTPUT_10MHZ UINT32_C(1)

#endif // REGISTERS_H