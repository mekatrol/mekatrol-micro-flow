
#include "usart.h"
#include "registers.h"
#include "rs485.h"

#include <stddef.h>
#include <stdint.h>
/*
 * USART1 configuration constants.
 *
 * hal_init() configures the CPU to run at 48 MHz.
 * With the default APB2 prescaler of 1, USART1
 * also receives a 48 MHz peripheral clock.
 *
 * If the APB2 prescaler changes later, this
 * constant and the baud-rate calculation must
 * be updated together.
 */
#define USART1_CLOCK_HZ UINT32_C(48000000)
#define USART1_BAUD UINT32_C(19200)

/*
 * In 16x oversampling mode, USART_BRR represents
 * the peripheral clock divided by the baud rate.
 *
 * 48,000,000 / 19,200 = 2,500 = 0x09C4.
 *
 * The divider consists of a 12-bit mantissa and
 * a four-bit fractional part.
 *
 * Mantissa = 2500 / 16 = 156
 * Fraction = 2500 % 16 = 4
 *
 * This gives the requested baud rate exactly,
 * assuming the peripheral clock is exactly 48 MHz.
 * Actual accuracy depends on oscillator accuracy.
 */
#define USART1_BRR_VALUE (USART1_CLOCK_HZ / USART1_BAUD)

_Static_assert(USART1_BRR_VALUE == UINT32_C(2500),
               "Unexpected USART1 baud-rate divider");

/*
 * Configure one GPIO pin without modifying
 * other pins sharing the same configuration
 * register.
 *
 * WHAT:
 * Replaces the four configuration bits belonging
 * to the selected pin.
 *
 * WHY:
 * PD5 and PD6 share GPIOD_CFGLR with other pins.
 * Changing unrelated fields could disturb output
 * channels or the SWIO interface.
 */
static void gpio_config_pin(volatile uint32_t *reg, uint32_t pin,
                            uint32_t configuration) {
  const uint32_t shift = pin * UINT32_C(4);
  const uint32_t mask = GPIO_CONFIG_FIELD_MASK << shift;

  *reg = (*reg & ~mask) | (configuration << shift);
}

void usart_init(void) {
  /*
   * Enable the alternate-function controller and
   * USART1 peripheral clock on APB2.
   *
   * USART1 registers are not operational while
   * their peripheral clock is disabled.
   *
   * hal_init() already enables GPIOD's clock.
   */
  RCC_APB2PCENR |= RCC_AFIOEN | RCC_USART1EN;

  /*
   * Explicitly select USART1's default pin mapping.
   *
   * WHAT:
   * Clear both USART1 remapping bits in AFIO_PCFR1.
   *
   * WHY:
   * Our PCB connects USART1 TX to PD5 and RX to PD6.
   * Clearing these bits ensures the peripheral uses
   * those pins rather than an alternative mapping.
   *
   * Preserve all unrelated remapping fields because
   * other peripherals may use this register later.
   */
  AFIO_PCFR1 &= ~AFIO_USART1_REMAP_MASK;

  /*
   * Keep USART1 disabled while configuring
   * the baud rate, word length and GPIO modes.
   *
   * Starting the UART partway through setup
   * could interpret noise as received data.
   */
  USART1_CTLR1 = 0U;

  /*
   * Configure PD5 as USART1 TX.
   *
   * Alternate-function push-pull allows the
   * USART peripheral to drive its TX waveform
   * instead of software-controlled GPIO output.
   */
  gpio_config_pin(&GPIOD_CFGLR, 5U, GPIO_AF_OUTPUT_10MHZ);

  /*
   * Configure PD6 as USART1 RX.
   *
   * A floating digital input allows the
   * external RS-485 transceiver's RO output
   * to determine the received logic level.
   *
   * The transceiver must provide a valid
   * logic HIGH/LOW signal on this pin.
   */
  gpio_config_pin(&GPIOD_CFGLR, 6U, GPIO_INPUT_FLOATING);

  /*
   * Program the baud-rate divider.
   *
   * The 48 MHz peripheral clock is divided
   * down to produce 19200 bits per second.
   */
  USART1_BRR = USART1_BRR_VALUE;

  /*
   * CTLR2 STOP[1:0] = 00 selects one stop bit.
   *
   * All other optional features are disabled
   * for the initial protocol implementation.
   */
  USART1_CTLR2 = 0U;

  /*
   * Disable DMA, hardware flow control and
   * additional USART features initially.
   *
   * Interrupt-driven reception uses the
   * hardware RXNE flag rather than RX DMA.
   *
   * Receive-error interrupts will be enabled
   * separately after the RX queue is ready.
   */
  USART1_CTLR3 = 0U;

  /*
   * Enable USART1, transmitter and receiver.
   *
   * M=0:   Eight data bits
   * PCE=0: No parity
   * STOP=0: One stop bit (configured above)
   *
   * RXNEIE remains disabled until the main
   * application has initialized the RX queue.
   */
  USART1_CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}

void usart_enable_rx_interrupts(void) {
  /*
   * Clear any stale received character or
   * receive-error condition before enabling
   * interrupt delivery.
   *
   * The CH32V003 requires reading STATR
   * followed by DATAR to complete this
   * receive/error clearing sequence.
   *
   * Bytes arriving before this point are
   * deliberately not treated as valid frames.
   */
  volatile uint32_t status = USART1_STATR;
  volatile uint32_t data = USART1_DATAR;

  (void)status;
  (void)data;

  /*
   * Enable USART receiver-related interrupt
   * sources:
   *
   * RXNEIE: Received data available
   * PEIE:   Parity error reporting
   * EIE:    Additional receive error reporting
   *
   * TXEIE and TCIE remain disabled because
   * the initial transmission implementation
   * will be controlled by the foreground loop.
   */
  USART1_CTLR3 |= USART_CTLR3_EIE;

  USART1_CTLR1 |= USART_CTLR1_RXNEIE | USART_CTLR1_PEIE;

  /*
   * Enable USART1 IRQ number 32 in the QingKe
   * Programmable Fast Interrupt Controller.
   *
   * Interrupt 32 maps to bit zero of IENR2.
   *
   * IENR2 is a write-one-to-enable register,
   * so write the required bit directly.
   * Do not use read-modify-write on this register.
   */
  PFIC_IENR2 = USART1_IRQ_ENABLE_MASK;

  /*
   * Enable machine-level interrupt delivery.
   *
   * startup.S installed the interrupt vector
   * and left global interrupts disabled.
   *
   * Setting MIE (bit 3) in mstatus allows
   * pending enabled peripheral interrupts to
   * reach their assembly interrupt wrappers.
   *
   * A memory clobber prevents the compiler
   * from moving memory operations across this
   * initialization boundary.
   */
  __asm__ volatile("csrsi mstatus, 8" : : : "memory");
}

void usart_write_response(const uint8_t *data, size_t length) {
  /*
   * A zero-length response requires no bus activity.
   *
   * Broadcast requests never receive replies, so
   * enabling DE for an empty response would be
   * unnecessary and could disturb another device.
   */
  if (length == 0U) {
    return;
  }

  /*
   * The caller must supply a valid byte buffer.
   *
   * This driver does not allocate or copy a response.
   * The caller owns the buffer and must keep it valid
   * until this blocking function returns.
   */
  if (data == NULL) {
    return;
  }

  /*
   * Ensure any previous transmission has finished
   * before starting another response.
   *
   * TC indicates that the UART is completely idle
   * after transmitting its final stop bit.
   *
   * This also ensures the previous response does
   * not overlap the new one.
   */
  while ((USART1_STATR & USART_STATR_TC) == 0U) {
    /* Wait for USART transmission complete. */
  }

  /*
   * Enable the RS-485 transmitter.
   *
   * PC6 HIGH enables DE and disables /RE because
   * both transceiver direction inputs are tied.
   *
   * The USART TX pin idles HIGH when enabled.
   * Therefore the bus driver starts by driving
   * the normal UART idle state.
   */
  rs485_transmit();

  /*
   * The transceiver requires a finite driver-
   * enable propagation time before the first
   * start bit may be transmitted.
   *
   * A short guard interval prevents the UART
   * from transmitting before DE has taken effect.
   *
   * At 48 MHz, a 1 us delay corresponds to
   * 48 SysTick counts. The actual transceiver
   * enable time must be checked against its
   * datasheet.
   */
  const uint32_t enable_start = SYSTICK_CNT;

  while ((uint32_t)(SYSTICK_CNT - enable_start) < UINT32_C(48)) {
    /* Wait approximately 1 microsecond. */
  }

  /*
   * Write each response byte to the transmit
   * data register.
   *
   * TXE becomes set when DATAR can accept
   * another character.
   *
   * Unlike TC, TXE does not require the
   * previous character's stop bit to finish.
   * This allows continuous UART transmission
   * without unnecessary inter-byte gaps.
   */
  for (size_t i = 0U; i < length; ++i) {
    /*
     * Wait until the transmit data register
     * is ready for the next byte.
     */
    while ((USART1_STATR & USART_STATR_TXE) == 0U) {
      /* Wait for space in USART DATAR. */
    }

    /*
     * DATAR is accessed as a 32-bit register,
     * but the UART sends only the low eight
     * bits because it is configured for 8N1.
     */
    USART1_DATAR = (uint32_t)data[i];
  }

  /*
   * Wait until the UART has finished sending
   * the final response character.
   *
   * TC differs from TXE:
   *
   * TXE = next byte can be loaded.
   * TC  = final stop bit has left TX.
   *
   * Disabling DE before TC would truncate
   * the response and corrupt the frame.
   */
  while ((USART1_STATR & USART_STATR_TC) == 0U) {
    /* Wait for final stop bit. */
  }

  /*
   * Release the shared RS-485 bus.
   *
   * PC6 LOW disables the transmitter and
   * enables the receiver, allowing the
   * next master request to arrive.
   *
   * This is the normal idle state of the
   * half-duplex interface.
   */
  rs485_receive();
}
