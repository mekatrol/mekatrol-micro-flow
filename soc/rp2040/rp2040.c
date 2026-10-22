#include "rp2040.h"

#include <stdint.h>

#define RP2040_RESETS_BASE 0x4000C000U
#define RP2040_CLOCKS_BASE 0x40008000U
#define RP2040_XOSC_BASE 0x40024000U
#define RP2040_IO_BANK0_BASE 0x40014000U
#define RP2040_SIO_BASE 0xD0000000U
#define RP2040_RESETS_RESET (*(volatile uint32_t *)(RP2040_RESETS_BASE + 0x00U))
#define RP2040_RESETS_DONE (*(volatile uint32_t *)(RP2040_RESETS_BASE + 0x08U))
#define RP2040_SIO_GPIO_OUT_SET (*(volatile uint32_t *)(RP2040_SIO_BASE + 0x14U))
#define RP2040_SIO_GPIO_OUT_CLR (*(volatile uint32_t *)(RP2040_SIO_BASE + 0x18U))
#define RP2040_SIO_GPIO_OE_SET (*(volatile uint32_t *)(RP2040_SIO_BASE + 0x24U))
#define RP2040_IO_FUNC_SIO 5U
#define RP2040_RESET_IO_BANK0 (1U << 5U)
#define RP2040_RESET_PADS_BANK0 (1U << 8U)
#define RP2040_XOSC_ENABLE (0xFABU << 12U)
#define RP2040_XOSC_FREQ_1_15_MHZ 0xAA0U
#define RP2040_XOSC_STABLE (1U << 31U)

static inline __attribute__((always_inline)) void microflow_rp2040_delay_cycles(uint32_t cycles) {
  while (cycles != 0U) {
    __asm volatile("nop");
    --cycles;
  }
}

static uint32_t microflow_rp2040_interrupt_state(void) {
  uint32_t value;
  __asm volatile("mrs %0, primask" : "=r"(value));
  return value;
}

static void microflow_rp2040_ws2812_bit(uint32_t mask, uint32_t value) {
  RP2040_SIO_GPIO_OUT_SET = mask;
  if (value != 0U) {
    microflow_rp2040_delay_cycles(2U);
    RP2040_SIO_GPIO_OUT_CLR = mask;
    microflow_rp2040_delay_cycles(1U);
  } else {
    microflow_rp2040_delay_cycles(1U);
    RP2040_SIO_GPIO_OUT_CLR = mask;
    microflow_rp2040_delay_cycles(2U);
  }
}

static void microflow_rp2040_ws2812_byte(uint32_t mask, uint8_t value) {
  uint32_t bit;

  for (bit = 0x80U; bit != 0U; bit >>= 1U) {
    microflow_rp2040_ws2812_bit(mask, (uint32_t)value & bit);
  }
}

void microflow_rp2040_initialize(void) {
  const uint32_t resets = RP2040_RESET_IO_BANK0 | RP2040_RESET_PADS_BANK0;
  volatile uint32_t *const xosc_control = (volatile uint32_t *)(RP2040_XOSC_BASE + 0x00U);
  volatile uint32_t *const xosc_status = (volatile uint32_t *)(RP2040_XOSC_BASE + 0x04U);
  volatile uint32_t *const xosc_startup = (volatile uint32_t *)(RP2040_XOSC_BASE + 0x0CU);
  volatile uint32_t *const clock_reference_control = (volatile uint32_t *)(RP2040_CLOCKS_BASE + 0x30U);
  volatile uint32_t *const clock_reference_selected = (volatile uint32_t *)(RP2040_CLOCKS_BASE + 0x38U);

  *xosc_startup = 47U;
  *xosc_control = RP2040_XOSC_ENABLE | RP2040_XOSC_FREQ_1_15_MHZ;
  while ((*xosc_status & RP2040_XOSC_STABLE) == 0U) {
  }
  *clock_reference_control = 2U;
  while ((*clock_reference_selected & (1U << 2U)) == 0U) {
  }

  RP2040_RESETS_RESET &= ~resets;
  while ((RP2040_RESETS_DONE & resets) != resets) {
  }
}

void microflow_rp2040_ws2812_initialize(uint32_t pin) {
  volatile uint32_t *const control = (volatile uint32_t *)(RP2040_IO_BANK0_BASE + 0x04U + (pin * 8U));
  *control = RP2040_IO_FUNC_SIO;
  RP2040_SIO_GPIO_OUT_CLR = 1U << pin;
  RP2040_SIO_GPIO_OE_SET = 1U << pin;
  microflow_rp2040_delay_ms(1U);
}

void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue) {
  const uint32_t mask = 1U << pin;
  const uint32_t interrupt_state = microflow_rp2040_interrupt_state();

  __asm volatile("cpsid i");
  microflow_rp2040_ws2812_byte(mask, green);
  microflow_rp2040_ws2812_byte(mask, red);
  microflow_rp2040_ws2812_byte(mask, blue);
  if (interrupt_state == 0U) {
    __asm volatile("cpsie i");
  }
  microflow_rp2040_delay_cycles(320U);
}

void microflow_rp2040_delay_ms(uint32_t milliseconds) {
  while (milliseconds != 0U) {
    microflow_rp2040_delay_cycles(4000U);
    --milliseconds;
  }
}
