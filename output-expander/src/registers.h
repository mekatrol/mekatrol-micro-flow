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

/*
 * USART1 receives its peripheral clock from APB2.
 *
 * The peripheral clock must be enabled before
 * accessing USART configuration registers.
 *
 * The firmware currently runs APB2 at 48 MHz,
 * allowing the USART baud-rate generator to
 * derive the required 19200 baud signal.
 */
#define RCC_USART1EN (UINT32_C(1) << 14)

/*
 * The alternate-function I/O peripheral controls
 * remapping of peripheral signals onto GPIO pins.
 *
 * Our production board uses the default USART1
 * mapping: PD5 = TX, PD6 = RX.
 */
#define RCC_AFIOEN (UINT32_C(1) << 0)

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

/*
 * CH32V003 GPIO configuration.
 *
 * Every GPIO pin has a four-bit configuration field:
 *
 *   CNF[1:0] MODE[1:0]
 *
 * USART1 TX (PD5):
 *   Alternate-function push-pull, 10 MHz.
 *   CNF = 10, MODE = 01 -> binary 1001.
 *
 * USART1 RX (PD6):
 *   Floating input.
 *   CNF = 01, MODE = 00 -> binary 0100.
 *
 * The 10 MHz output-drive setting is adequate
 * for a 19200 baud UART signal.
 */
#define GPIO_AF_OUTPUT_10MHZ UINT32_C(0x9)
#define GPIO_INPUT_FLOATING UINT32_C(0x4)

/*
 * GPIO configuration fields are four bits wide.
 * Masking the field before writing prevents the
 * USART configuration from changing adjacent pins.
 */
#define GPIO_CONFIG_FIELD_MASK UINT32_C(0xF)

/*
 * USART1 uses its default pin routing:
 * PD5 = TX, PD6 = RX.
 *
 * Do not enable a USART remap that would move
 * the signals away from the PCB's assigned pins.
 */

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

/*
 * USART1 memory-mapped registers.
 *
 * The CH32V003 has one hardware USART peripheral,
 * located at base address 0x40013800.
 *
 * STATR: Reports received data, transmission status
 *        and communication errors.
 *
 * DATAR: Holds the next received byte or the byte
 *        waiting to be transmitted.
 *
 * BRR:   Controls the baud-rate generator.
 *
 * CTLR1: Enables USART, transmitter, receiver and
 *        selected interrupt sources.
 *
 * CTLR2: Configures stop bits and optional features.
 *
 * CTLR3: Configures additional features and
 *        communication error interrupts.
 */
#define USART1_STATR REG32(UINT32_C(0x40013800))
#define USART1_DATAR REG32(UINT32_C(0x40013804))
#define USART1_BRR REG32(UINT32_C(0x40013808))
#define USART1_CTLR1 REG32(UINT32_C(0x4001380C))
#define USART1_CTLR2 REG32(UINT32_C(0x40013810))
#define USART1_CTLR3 REG32(UINT32_C(0x40013814))

/*
 * USART1 status register (STATR).
 *
 * These flags allow the driver and interrupt
 * handler to distinguish received bytes from
 * transmission events and UART errors.
 */

/* Parity error: received parity was incorrect. */
#define USART_STATR_PE (UINT32_C(1) << 0)

/* Framing error: invalid stop-bit detection. */
#define USART_STATR_FE (UINT32_C(1) << 1)

/* Noise detected while receiving a character. */
#define USART_STATR_NE (UINT32_C(1) << 2)

/*
 * Overrun: a new character arrived before the
 * previous received character was consumed.
 *
 * The current protocol frame must be rejected
 * because one or more bytes may have been lost.
 */
#define USART_STATR_ORE (UINT32_C(1) << 3)

/* Receive data register contains a character. */
#define USART_STATR_RXNE (UINT32_C(1) << 5)

/*
 * Transmission complete.
 *
 * TC becomes set when the final character,
 * including its stop bit, has finished transmitting.
 *
 * RS-485 DE must not be released before TC.
 */
#define USART_STATR_TC (UINT32_C(1) << 6)

/*
 * Transmit data register empty.
 *
 * TXE means another byte can be written to DATAR.
 * It does NOT mean the final byte has finished
 * transmitting on the physical TX pin.
 */
#define USART_STATR_TXE (UINT32_C(1) << 7)

/* All receive-error flags handled by our driver. */
#define USART_STATR_RX_ERRORS                                                  \
  (USART_STATR_PE | USART_STATR_FE | USART_STATR_NE | USART_STATR_ORE)

/*
 * USART control register 1 (CTLR1).
 *
 * The protocol uses 19200 baud, 8 data bits,
 * no parity and one stop bit (8N1).
 *
 * Word length M=0 and parity PCE=0 select
 * eight data bits without a parity bit.
 */

/* Enable the receiver hardware. */
#define USART_CTLR1_RE (UINT32_C(1) << 2)

/* Enable the transmitter hardware. */
#define USART_CTLR1_TE (UINT32_C(1) << 3)

/*
 * Generate an interrupt when a received
 * character is available in DATAR.
 *
 * The interrupt handler will read this byte
 * and push it into our fixed-size RX queue.
 */
#define USART_CTLR1_RXNEIE (UINT32_C(1) << 5)

/* Generate an interrupt when transmission ends. */
#define USART_CTLR1_TCIE (UINT32_C(1) << 6)

/* Generate an interrupt when TX register is empty. */
#define USART_CTLR1_TXEIE (UINT32_C(1) << 7)

/* Enable parity-error interrupt reporting. */
#define USART_CTLR1_PEIE (UINT32_C(1) << 8)

/* Enable the USART peripheral. */
#define USART_CTLR1_UE (UINT32_C(1) << 13)

/*
 * USART control register 2 (CTLR2).
 *
 * STOP[1:0] occupies bits 13:12.
 * Both bits clear selects one stop bit.
 */
#define USART_CTLR2_STOP_MASK (UINT32_C(3) << 12)

/*
 * USART control register 3 (CTLR3).
 *
 * EIE enables additional receive-error interrupt
 * reporting. The handler will also check STATR
 * for framing, noise and overrun conditions.
 */
#define USART_CTLR3_EIE (UINT32_C(1) << 0)

/*
 * QingKe Programmable Fast Interrupt Controller.
 *
 * PFIC controls which hardware interrupts are
 * permitted to reach the CPU.
 *
 * USART1 uses interrupt number 32, which maps
 * to bit 0 of the second interrupt-enable
 * register (IENR2).
 *
 * Enabling this controller bit alone is not
 * sufficient: USART receive/error interrupts
 * must also be enabled inside USART1.
 *
 * We will configure the interrupt vector table
 * and CPU interrupt state before using this.
 */
#define PFIC_IENR2 REG32(UINT32_C(0xE000E104))

#define USART1_IRQ_NUMBER 32U
#define USART1_IRQ_ENABLE_MASK UINT32_C(1)

/*
 * Alternate-function remapping register.
 *
 * USART1 uses two remapping bits:
 * bit 2 and bit 21.
 *
 * Both must be zero for the default mapping:
 * PD5 = TX and PD6 = RX.
 */
#define AFIO_PCFR1 REG32(UINT32_C(0x40010004))

#define AFIO_USART1_REMAP_MASK ((UINT32_C(1) << 2) | (UINT32_C(1) << 21))

#endif // REGISTERS_H