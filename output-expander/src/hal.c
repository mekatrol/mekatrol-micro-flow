#include "registers.h"
#include "hal.h"

#include <stdint.h>

static void gpio_config_output(volatile uint32_t *config, uint32_t pin) {
  /* A pin occupies one four-bit field: MODE is the low two bits and CNF is
   * the high two. Writing 0001 selects a 10 MHz general-purpose push-pull
   * output. Ten megahertz is ample for relay/LED control and causes less
   * switching noise than the fastest 50 MHz mode. */
  const uint32_t shift = pin * UINT32_C(4);
  *config =
      (*config & ~(UINT32_C(0xF) << shift)) | (GPIO_OUTPUT_10MHZ << shift);
}

void hal_init(void) {
  /* Set the Flash latency before accelerating the CPU. Doing this in the
   * opposite order could make the next instruction fetch unreliable. */
  FLASH_ACTLR = (FLASH_ACTLR & ~UINT32_C(3)) | UINT32_C(1);

  /* Select the calibrated 24 MHz internal HSI as the PLL input. The
   * CH32V003 PLL multiplier is fixed at x2, producing a 48 MHz system clock
   * without depending on an external crystal. */
  RCC_CFGR0 &= ~(UINT32_C(3) << 16);
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

  /* Preload all eight active-low outputs HIGH while the pins are still
   * inputs. When each pin becomes an output it therefore starts OFF without
   * a momentary LOW pulse that could energize a relay or load. */
  GPIOC_BSHR = (UINT32_C(1) << 0) | (UINT32_C(1) << 3) | (UINT32_C(1) << 4) |
               (UINT32_C(1) << 5);
  GPIOD_BSHR = (UINT32_C(1) << 0) | (UINT32_C(1) << 2) | (UINT32_C(1) << 3) |
               (UINT32_C(1) << 4);
  /* PC6 controls tied RS-485 DE and active-low /RE. Preload it LOW before
   * enabling output mode so the transceiver remains receive-safe and never
   * drives the shared bus during startup. */
  GPIOC_BCR = UINT32_C(1) << 6;

  /* Configure the fixed board mapping. PC0/3/4/5 and PD0/2/3/4 are the eight
   * active-low outputs; PC6 is the RS-485 direction signal. PD1 is
   * intentionally untouched because it is the one-wire SWIO programming
   * pin needed to reflash and recover the device. */
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
   * long as the requested interval is less than one full counter period. */
  const uint32_t start = SYSTICK_CNT;
  while ((uint32_t)(SYSTICK_CNT - start) < UINT32_C(24000000)) {
  }
}

void hal_toggle_outputs(void) {
  /* All eight outputs are active-low and initially HIGH/OFF. */
  static uint32_t output_is_high = UINT32_C(1);

  /* PC0, PC3, PC4, PC5 */
  const uint32_t port_c_mask = (UINT32_C(1) << 0) | (UINT32_C(1) << 3) |
                               (UINT32_C(1) << 4) | (UINT32_C(1) << 5);

  /* PD0, PD2, PD3, PD4 */
  const uint32_t port_d_mask = (UINT32_C(1) << 0) | (UINT32_C(1) << 2) |
                               (UINT32_C(1) << 3) | (UINT32_C(1) << 4);

  if (output_is_high != 0U) {
    /* LOW = all eight outputs ON */
    GPIOC_BCR = port_c_mask;
    GPIOD_BCR = port_d_mask;
    output_is_high = 0U;
  } else {
    /* HIGH = all eight outputs OFF */
    GPIOC_BSHR = port_c_mask;
    GPIOD_BSHR = port_d_mask;
    output_is_high = UINT32_C(1);
  }
}
