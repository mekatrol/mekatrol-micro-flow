
#include "rp2040.h"
#include "rp2040_regs.h"

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

static inline void microflow_rp2040_delay_cycles(uint32_t cycles) {
    while (cycles != 0U) {
        __asm volatile("nop" ::: "memory");
        --cycles;
    }
}

static inline uint32_t microflow_rp2040_interrupt_state(void) {
    uint32_t value;

    __asm volatile("mrs %0, primask" : "=r"(value)::"memory");

    return value;
}

static inline void microflow_rp2040_interrupt_disable(void) {
    __asm volatile("cpsid i" ::: "memory");
}

static inline void microflow_rp2040_interrupt_restore(uint32_t state) {
    __asm volatile("msr primask, %0" : : "r"(state) : "memory");
}

/* -------------------------------------------------------------------------- */
/* WS2812                                                                      */
/* -------------------------------------------------------------------------- */

/*
 * Software-generated WS2812 waveform.
 *
 * WARNING:
 * Timing depends on CPU frequency and compiler output.
 * This implementation must be calibrated before use.
 * RP2040 PIO is recommended for reliable WS2812 communication.
 */

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
    for (uint32_t bit = 0x80U; bit != 0U; bit >>= 1U) {
        microflow_rp2040_ws2812_bit(mask, (uint32_t)value & bit);
    }
}

/* -------------------------------------------------------------------------- */
/* Platform initialization                                                    */
/* -------------------------------------------------------------------------- */

void microflow_rp2040_initialize(void) {
    const uint32_t resets =
        RP2040_RESET_IO_BANK0 | RP2040_RESET_PADS_BANK0;

    /* Start external crystal oscillator */
    RP2040_XOSC_STARTUP = RP2040_XOSC_STARTUP_COUNT;
    RP2040_XOSC_CTRL    = RP2040_XOSC_ENABLE | RP2040_XOSC_FREQ_1_15_MHZ;

    while ((RP2040_XOSC_STATUS & RP2040_XOSC_STABLE) == 0U) {
    }

    /* Select XOSC as reference clock */
    RP2040_CLK_REF_CTRL = RP2040_CLK_REF_SRC_XOSC;

    while ((RP2040_CLK_REF_SELECTED & RP2040_CLK_REF_SELECTED_XOSC) == 0U) {
    }

    /* Release IO_BANK0 and PADS_BANK0 from reset */
    RP2040_RESETS_RESET &= ~resets;

    while ((RP2040_RESETS_DONE & resets) != resets) {
    }
}

/* -------------------------------------------------------------------------- */
/* WS2812 public functions                                                    */
/* -------------------------------------------------------------------------- */

void microflow_rp2040_ws2812_initialize(uint32_t pin) {
    if (pin >= RP2040_GPIO_COUNT) {
        return;
    }

    RP2040_GPIO_CTRL(pin)   = RP2040_IO_FUNC_SIO;
    RP2040_SIO_GPIO_OUT_CLR = 1U << pin;
    RP2040_SIO_GPIO_OE_SET  = 1U << pin;

    microflow_rp2040_delay_ms(1U);
}

void microflow_rp2040_ws2812_write(
    uint32_t pin,
    uint8_t  red,
    uint8_t  green,
    uint8_t  blue) {
    if (pin >= RP2040_GPIO_COUNT) {
        return;
    }

    const uint32_t mask = 1U << pin;

    const uint32_t interrupt_state =
        microflow_rp2040_interrupt_state();

    microflow_rp2040_interrupt_disable();

    /* WS2812 expects GRB ordering */
    microflow_rp2040_ws2812_byte(mask, green);
    microflow_rp2040_ws2812_byte(mask, red);
    microflow_rp2040_ws2812_byte(mask, blue);

    microflow_rp2040_interrupt_restore(interrupt_state);

    /* Reset/latch interval */
    microflow_rp2040_delay_ms(1U);
}

/* -------------------------------------------------------------------------- */
/* Timing                                                                      */
/* -------------------------------------------------------------------------- */

void microflow_rp2040_delay_ms(uint32_t milliseconds) {
    while (milliseconds != 0U) {
        const uint32_t start = RP2040_TIMER_TIMERAWL;

        while ((uint32_t)(RP2040_TIMER_TIMERAWL - start) <
               RP2040_MICROSECONDS_PER_MILLISECOND) {
        }

        --milliseconds;
    }
}
