#include "esp32.h"

#include "microflow_esp32_soc.h"

#include <stdint.h>

#define ESP32_GPIO_OUT_W1TS (MICROFLOW_ESP32_GPIO_BASE + 0x08U)
#define ESP32_GPIO_OUT_W1TC (MICROFLOW_ESP32_GPIO_BASE + 0x0CU)
#define ESP32_GPIO_ENABLE_W1TS (MICROFLOW_ESP32_GPIO_BASE + 0x24U)
#define ESP32_GPIO_FUNC_OUT_SEL(pin) (MICROFLOW_ESP32_GPIO_BASE + 0x554U + ((pin) * 4U))
#define ESP32_IOMUX_MCU_SEL_MASK (7U << 12U)
#define ESP32_IOMUX_MCU_SEL_GPIO (1U << 12U)

static inline void microflow_register_write(uint32_t address, uint32_t value) {
  *(volatile uint32_t *)address = value;
}

static inline uint32_t microflow_register_read(uint32_t address) {
  return *(volatile uint32_t *)address;
}

static inline void microflow_esp32_delay_cycles(uint32_t cycles) {
  while (cycles != 0U) {
    __asm volatile("nop");
    --cycles;
  }
}

static inline void microflow_esp32_ws2812_bit(uint32_t mask, uint32_t value) {
  microflow_register_write(ESP32_GPIO_OUT_W1TS, mask);
  if (value != 0U) {
    microflow_esp32_delay_cycles(MICROFLOW_ESP32_WS2812_ONE_HIGH_CYCLES);
    microflow_register_write(ESP32_GPIO_OUT_W1TC, mask);
    microflow_esp32_delay_cycles(MICROFLOW_ESP32_WS2812_ONE_LOW_CYCLES);
  } else {
    microflow_esp32_delay_cycles(MICROFLOW_ESP32_WS2812_ZERO_HIGH_CYCLES);
    microflow_register_write(ESP32_GPIO_OUT_W1TC, mask);
    microflow_esp32_delay_cycles(MICROFLOW_ESP32_WS2812_ZERO_LOW_CYCLES);
  }
}

static void microflow_esp32_ws2812_byte(uint32_t mask, uint8_t value) {
  uint32_t bit;

  for (bit = 0x80U; bit != 0U; bit >>= 1U) {
    microflow_esp32_ws2812_bit(mask, (uint32_t)value & bit);
  }
}

void microflow_esp32_initialize(void) {
}

void microflow_esp32_ws2812_initialize(uint32_t pin) {
  const uint32_t iomux_address = MICROFLOW_ESP32_IOMUX_BASE + MICROFLOW_ESP32_IOMUX_OFFSET(pin);
  uint32_t iomux = microflow_register_read(iomux_address);
  iomux &= ~ESP32_IOMUX_MCU_SEL_MASK;
  iomux |= ESP32_IOMUX_MCU_SEL_GPIO;
  microflow_register_write(iomux_address, iomux);
  microflow_register_write(ESP32_GPIO_FUNC_OUT_SEL(pin), MICROFLOW_ESP32_GPIO_OUTPUT_SIGNAL);
  microflow_register_write(ESP32_GPIO_OUT_W1TC, 1U << pin);
  microflow_register_write(ESP32_GPIO_ENABLE_W1TS, 1U << pin);
  microflow_esp32_delay_ms(1U);
}

void microflow_esp32_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue) {
  const uint32_t mask = 1U << pin;
  microflow_esp32_ws2812_byte(mask, green);
  microflow_esp32_ws2812_byte(mask, red);
  microflow_esp32_ws2812_byte(mask, blue);
  microflow_esp32_delay_cycles(MICROFLOW_ESP32_CPU_MHZ * 80U);
}

void microflow_esp32_delay_ms(uint32_t milliseconds) {
  while (milliseconds != 0U) {
    microflow_esp32_delay_cycles(MICROFLOW_ESP32_CPU_MHZ * 500U);
    --milliseconds;
  }
}
