#include "hal.h"
#include "registers.h"

#include <stdint.h>

/* Keep the board's output polarity in the HAL so applications select logical
 * ON/OFF states without knowing whether a particular GPIO must be HIGH or LOW.
 * Each bit corresponds to one of the eight logical outputs (bit 0 = output 1).
 * hal_init() sets this before any output-level operation is performed. */
static uint8_t hal_active_low_mask;

static void gpio_config_output(volatile uint32_t *config, uint32_t pin) {
  /* Each GPIO pin has a four-bit configuration field: MODE in the low two
   * bits and CNF in the high two bits. The 0001 setting configures a 10 MHz
   * general-purpose push-pull output. This speed is sufficient for LEDs and
   * relays and avoids selecting the fastest available output drive mode. */
  const uint32_t shift = pin * UINT32_C(4);
  *config =
      (*config & ~(UINT32_C(0xF) << shift)) | (GPIO_OUTPUT_10MHZ << shift);
}

static uint32_t output_port_c(uint8_t output_mask) {
  /* Logical output bits 0-3 map to PC0, PC3, PC4 and PC5 respectively.
   * Keep bit 0 in place and shift bits 1-3 up by two positions. Masking
   * first ensures that outputs on port D cannot affect unrelated PC pins. */
  return ((uint32_t)(output_mask & 0x01U)) |
         ((uint32_t)(output_mask & 0x0EU) << 2);
}

static uint32_t output_port_d(uint8_t output_mask) {
  /* Logical output bits 4-7 map to PD0, PD2, PD3 and PD4 respectively.
   * Move bit 4 down to bit 0 and bits 5-7 down to bits 2-4. In particular,
   * PD1 is never selected because it is the SWIO programming/debug pin. */
  return ((uint32_t)(output_mask & 0x10U) >> 4) |
         ((uint32_t)(output_mask & 0xE0U) >> 3);
}

static void hal_output_write(uint8_t output_mask, uint8_t on) {
  /* Select the GPIO level required for each chosen logical output:
   *   ON:  active-high -> HIGH, active-low -> LOW.
   *   OFF: active-high -> LOW,  active-low -> HIGH.
   * Only outputs selected in output_mask are included; all others must
   * retain their current electrical state. */
  const uint8_t high_mask =
      on ? (uint8_t)(output_mask & (uint8_t)~hal_active_low_mask)
         : (uint8_t)(output_mask & hal_active_low_mask);

  /* Every selected output that is not driven HIGH must be driven LOW.
   * XOR is safe because high_mask is always a subset of output_mask. */
  const uint8_t low_mask = (uint8_t)(output_mask ^ high_mask);

  /* CH32V003 GPIOx_BSHR is write-only set/reset control: bits 0-7 drive
   * pins HIGH and bits 16-23 drive pins LOW; writing zero has no effect.
   * Combining the two masks gives a single atomic write per port, without
   * read-modify-write races or disturbing unselected GPIO pins (e.g. PC6). */
  GPIOC_BSHR = output_port_c(high_mask) | (output_port_c(low_mask) << 16);
  GPIOD_BSHR = output_port_d(high_mask) | (output_port_d(low_mask) << 16);
}

void hal_init(uint8_t active_low_mask) {
  /* Save the output polarity before setting startup levels. The same mask
   * is used by all later ON/OFF operations, so polarity remains consistent. */
  hal_active_low_mask = active_low_mask;

  /* Set Flash latency before accelerating the CPU. Doing this in the
   * opposite order could make the next instruction fetch unreliable. */
  FLASH_ACTLR = (FLASH_ACTLR & ~UINT32_C(3)) | UINT32_C(1);

  /*
   * Configure the AHB prescaler to divide by 1.
   *
   * WHAT:
   * Clear HPRE[3:0] in RCC_CFGR0.
   *
   * WHY:
   * The PLL produces 48 MHz, but HCLK may be
   * divided by the AHB prescaler.
   *
   * Our USART baud-rate calculation and SysTick
   * delays both assume a 48 MHz peripheral/CPU
   * clock. Explicitly configuring HPRE prevents
   * us from inheriting an unexpected divider.
   */
  RCC_CFGR0 &= ~RCC_HPRE_MASK;

  /*
   * Select the internal 24 MHz HSI as PLL input.
   *
   * PLLSRC is bit 16. Clearing it selects HSI.
   * The PLL multiplier is fixed at two, producing
   * a nominal 48 MHz system clock.
   */
  RCC_CFGR0 &= ~(UINT32_C(1) << 16);

  /*
   * Enable the PLL.
   *
   * The existing initialization waits for PLLRDY
   * before selecting PLL as the system clock.
   */
  RCC_CTLR |= RCC_PLLON;

  /* The PLL must lock before it can clock the CPU. There is deliberately no
   * timeout in this minimal image: running with an unlocked clock would make
   * every timing assumption invalid and is not a recoverable state. */
  while ((RCC_CTLR & RCC_PLLRDY) == 0U) {
  }

  /* Request PLL as SYSCLK, then wait for the status field to confirm that
   * hardware completed the switch before configuring time-based behavior. */
  RCC_CFGR0 = (RCC_CFGR0 & ~UINT32_C(3)) | RCC_SW_PLL;
  while ((RCC_CFGR0 & RCC_SWS_MASK) != RCC_SWS_PLL) {
  }

  /* GPIO registers ignore accesses while their peripheral clocks are gated,
   * so enable ports C and D before establishing output levels and modes. */
  RCC_APB2PCENR |= RCC_IOPCEN | RCC_IOPDEN;

  /* Preload the OFF level for all eight outputs while the pins are still
   * inputs. Active-low pins are driven HIGH and active-high pins are driven
   * LOW. When output mode is subsequently enabled, the GPIO output latch
   * already contains the correct inactive level, avoiding an unwanted pulse.
   * This does not control pin voltage during hardware reset itself. */
  hal_output_off(UINT8_MAX);

  /* PC6 controls tied RS-485 DE and active-low /RE. Preload it LOW before
   * enabling output mode so the transceiver remains receive-safe and never
   * drives the shared bus during startup. The eight-output masks omit PC6. */
  GPIOC_BCR = UINT32_C(1) << 6;

  /* Configure the fixed board mapping. PC0/3/4/5 and PD0/2/3/4 are the
   * eight logical outputs; their electrical ON/OFF polarity comes from
   * active_low_mask. PC6 is the RS-485 direction signal. PD1 is deliberately
   * untouched because it is the SWIO pin needed to reflash/recover the MCU. */
  gpio_config_output(&GPIOC_CFGLR, 0U);
  gpio_config_output(&GPIOC_CFGLR, 3U);
  gpio_config_output(&GPIOC_CFGLR, 4U);
  gpio_config_output(&GPIOC_CFGLR, 5U);
  gpio_config_output(&GPIOC_CFGLR, 6U);
  gpio_config_output(&GPIOD_CFGLR, 0U);
  gpio_config_output(&GPIOD_CFGLR, 2U);
  gpio_config_output(&GPIOD_CFGLR, 3U);
  gpio_config_output(&GPIOD_CFGLR, 4U);

  /* Start SysTick from zero using HCLK directly rather than HCLK/8. At
   * 48 MHz this gives 48,000,000 ticks per second and makes the 500 ms delay
   * exactly 24,000,000 ticks. Interrupt and auto-reload bits remain clear. */
  SYSTICK_CNT = 0U;
  SYSTICK_CTLR = (UINT32_C(1) << 2) | UINT32_C(1);
}

void hal_delay_half_second(void) {
  /* Unsigned subtraction remains correct when the 32-bit counter wraps, as
   * long as the requested interval is less than one full counter period.
   * Busy-waiting is intentional in this minimal, interrupt-free test image. */
  const uint32_t start = SYSTICK_CNT;
  while ((uint32_t)(SYSTICK_CNT - start) < UINT32_C(24000000)) {
  }
}

void hal_output_off(uint8_t output_mask) {
  /* Apply the configured polarity to selected outputs only. Passing OFF
   * drives active-low GPIO pins HIGH and active-high GPIO pins LOW. */
  hal_output_write(output_mask, 0U);
}

void hal_output_on(uint8_t output_mask) {
  /* Apply the configured polarity to selected outputs only. Passing ON
   * drives active-low GPIO pins LOW and active-high GPIO pins HIGH. */
  hal_output_write(output_mask, 1U);
}

uint32_t hal_ticks(void) { return SYSTICK_CNT; }
