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
#define RCC_AHBPCENR REG32(UINT32_C(0x40021014))
#define RCC_APB1PCENR REG32(UINT32_C(0x4002101C))

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
#define RCC_TIM1EN (UINT32_C(1) << 11)
#define RCC_DMA1EN (UINT32_C(1) << 0)
#define RCC_PWREN (UINT32_C(1) << 28)

/* Programmable voltage detector. EXTI line 8 rises when VDD falls below the
 * selected PLS threshold and is routed to dedicated interrupt number 17. */
#define PWR_CTLR REG32(UINT32_C(0x40007000))
#define PWR_CSR REG32(UINT32_C(0x40007004))
#define PWR_CTLR_PVDE (UINT32_C(1) << 4)
#define PWR_CTLR_PLS_MASK (UINT32_C(7) << 5)
#define PWR_CSR_PVDO (UINT32_C(1) << 2)

#define EXTI_INTENR REG32(UINT32_C(0x40010400))
#define EXTI_RTENR REG32(UINT32_C(0x40010408))
#define EXTI_FTENR REG32(UINT32_C(0x4001040C))
#define EXTI_INTFR REG32(UINT32_C(0x40010414))
#define EXTI_PVD_LINE (UINT32_C(1) << 8)

/*
 * RCC_CFGR0 HPRE[3:0] occupies bits 7:4.
 *
 * These bits divide the selected system clock
 * before distributing HCLK to the CPU and
 * peripherals.
 *
 * HPRE = 0000 selects division by 1.
 */
#define RCC_HPRE_MASK (UINT32_C(0xF) << 4)

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
#define GPIO_AF_OUTPUT_50MHZ UINT32_C(0xB)
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
#define PFIC_IENR1 REG32(UINT32_C(0xE000E100))
#define PFIC_IENR2 REG32(UINT32_C(0xE000E104))

#define PVD_IRQ_ENABLE_MASK (UINT32_C(1) << 17)
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

/* Production WS2812: PC7/TIM1_CH2 remap group 1 and DMA1 channel 3. */
#define AFIO_TIM1_RM_MASK (UINT32_C(3) << 6)
#define AFIO_TIM1_RM_GROUP1 (UINT32_C(1) << 6)

#define TIM1_CTLR1 REG32(UINT32_C(0x40012C00))
#define TIM1_CTLR2 REG32(UINT32_C(0x40012C04))
#define TIM1_DMAINTENR REG32(UINT32_C(0x40012C0C))
#define TIM1_INTFR REG32(UINT32_C(0x40012C10))
#define TIM1_SWEVGR REG32(UINT32_C(0x40012C14))
#define TIM1_CHCTLR1 REG32(UINT32_C(0x40012C18))
#define TIM1_CCER REG32(UINT32_C(0x40012C20))
#define TIM1_CNT REG32(UINT32_C(0x40012C24))
#define TIM1_PSC REG32(UINT32_C(0x40012C28))
#define TIM1_ATRLR REG32(UINT32_C(0x40012C2C))
#define TIM1_RPTCR REG32(UINT32_C(0x40012C30))
#define TIM1_CH2CVR REG32(UINT32_C(0x40012C38))
#define TIM1_BDTR REG32(UINT32_C(0x40012C44))

#define TIM_CTLR1_CEN (UINT32_C(1) << 0)
#define TIM_CTLR1_ARPE (UINT32_C(1) << 7)
#define TIM_CTLR2_CCDS (UINT32_C(1) << 3)
#define TIM_DMAINTENR_CC2DE (UINT32_C(1) << 10)
#define TIM_INTFR_UIF (UINT32_C(1) << 0)
#define TIM_SWEVGR_UG (UINT32_C(1) << 0)
#define TIM_CHCTLR1_OC2PE (UINT32_C(1) << 11)
#define TIM_CHCTLR1_OC2M_PWM1 (UINT32_C(6) << 12)
#define TIM_CCER_CC2E (UINT32_C(1) << 4)
#define TIM_BDTR_MOE (UINT32_C(1) << 15)

#define DMA1_INTFCR REG32(UINT32_C(0x40020004))
#define DMA1_INTFR REG32(UINT32_C(0x40020000))
#define DMA1_CH3_CFGR REG32(UINT32_C(0x40020030))
#define DMA1_CH3_CNTR REG32(UINT32_C(0x40020034))
#define DMA1_CH3_PADDR REG32(UINT32_C(0x40020038))
#define DMA1_CH3_MADDR REG32(UINT32_C(0x4002003C))
#define DMA_CFGR_EN (UINT32_C(1) << 0)
#define DMA_CFGR_DIR (UINT32_C(1) << 4)
#define DMA_CFGR_MINC (UINT32_C(1) << 7)
#define DMA_CFGR_PSIZE_16 (UINT32_C(1) << 8)
#define DMA_CFGR_MSIZE_16 (UINT32_C(1) << 10)
#define DMA_CFGR_PL_HIGH (UINT32_C(2) << 12)
#define DMA_CH3_CLEAR_MASK (UINT32_C(0xF) << 8)
#define DMA_CH3_TCIF (UINT32_C(1) << 9)

#endif // REGISTERS_H
