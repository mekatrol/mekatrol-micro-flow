/* Direct-register RP2040 implementation for freestanding board builds. */
#include "rp2040.h"
#include "rp2040_regs.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t microflow_rp2040_watchdog_load;

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

/* Burn a known small number of CPU cycles for the provisional WS2812 waveform. */
static inline void microflow_rp2040_delay_cycles(uint32_t cycles) {
    while (cycles != 0U) {
        __asm volatile("nop" ::: "memory");
        --cycles;
    }
}

/* Read PRIMASK so critical sections can restore, not assume, prior IRQ state. */
static inline uint32_t microflow_rp2040_interrupt_state(void) {
    uint32_t value;

    __asm volatile("mrs %0, primask" : "=r"(value)::"memory");

    return value;
}

/* Mask normal interrupts for the shortest timing/shared-state critical regions. */
static inline void microflow_rp2040_interrupt_disable(void) {
    __asm volatile("cpsid i" ::: "memory");
}

/* Restore the exact PRIMASK value captured before entering a critical region. */
static inline void microflow_rp2040_interrupt_restore(uint32_t state) {
    __asm volatile("msr primask, %0" : : "r"(state) : "memory");
}

/* Save and mask interrupts through the public SoC critical-section interface. */
uint32_t microflow_rp2040_interrupt_lock(void) {
    const uint32_t state = microflow_rp2040_interrupt_state();
    microflow_rp2040_interrupt_disable();
    return state;
}

/* End a public critical section by reinstating its caller's interrupt state. */
void microflow_rp2040_interrupt_unlock(uint32_t state) {
    microflow_rp2040_interrupt_restore(state);
}

/* -------------------------------------------------------------------------- */
/* Platform initialization                                                    */
/* -------------------------------------------------------------------------- */

/* Start the crystal/reference clock and release every required peripheral. */
void microflow_rp2040_initialize(void) {
    const uint32_t resets =
        RP2040_RESET_IO_BANK0 | RP2040_RESET_PADS_BANK0 |
        RP2040_RESET_TIMER | RP2040_RESET_UART0;

    /* Start external crystal oscillator */
    RP2040_XOSC_STARTUP = RP2040_XOSC_STARTUP_COUNT;
    RP2040_XOSC_CTRL    = RP2040_XOSC_ENABLE | RP2040_XOSC_FREQ_1_15_MHZ;

    while ((RP2040_XOSC_STATUS & RP2040_XOSC_STABLE) == 0U) {
    }

    /* Select XOSC as reference clock */
    RP2040_CLK_REF_CTRL = RP2040_CLK_REF_SRC_XOSC;

    while ((RP2040_CLK_REF_SELECTED & RP2040_CLK_REF_SELECTED_XOSC) == 0U) {
    }

    /* Release every peripheral used by the board support code from reset. */
    RP2040_RESETS_RESET &= ~resets;

    while ((RP2040_RESETS_DONE & resets) != resets) {
    }
}

/* Configure register-level UART0 as an interrupt-driven 8-N-1 transport. */
void microflow_rp2040_uart0_initialize(uint32_t baud_rate) {
    if (baud_rate == 0U) {
        return;
    }
    RP2040_CLK_PERI_CTRL = RP2040_CLK_PERI_ENABLE;
    RP2040_GPIO_CTRL(0U) = RP2040_IO_FUNC_UART;
    RP2040_GPIO_CTRL(1U) = RP2040_IO_FUNC_UART;
    RP2040_UART0_CR      = 0U;
    const uint32_t divisor_x64 =
        (RP2040_XOSC_FREQUENCY_HZ * 4U + baud_rate / 2U) / baud_rate;
    RP2040_UART0_IBRD  = divisor_x64 / 64U;
    RP2040_UART0_FBRD  = divisor_x64 % 64U;
    RP2040_UART0_LCR_H = RP2040_UART_LCR_H_FEN | RP2040_UART_LCR_H_WLEN_8;
    RP2040_UART0_IFLS  = 0U;
    RP2040_UART0_ICR   = 0x7FFU;
    RP2040_UART0_IMSC  = RP2040_UART_INT_RX | RP2040_UART_INT_RT;
    RP2040_UART0_CR    = RP2040_UART_CR_UARTEN | RP2040_UART_CR_TXE | RP2040_UART_CR_RXE;
    RP2040_NVIC_ICPR   = 1U << RP2040_IRQ_UART0;
    RP2040_NVIC_ISER   = 1U << RP2040_IRQ_UART0;
}

/* Tell interrupt code whether a UART read can complete immediately. */
bool microflow_rp2040_uart0_read_ready(void) {
    return (RP2040_UART0_FR & RP2040_UART_FR_RXFE) == 0U;
}

/* Remove one known-available byte from UART0's receive FIFO. */
uint8_t microflow_rp2040_uart0_read(void) {
    return (uint8_t)RP2040_UART0_DR;
}

/* Tell interrupt code whether UART0 can accept a byte immediately. */
bool microflow_rp2040_uart0_write_ready(void) {
    return (RP2040_UART0_FR & RP2040_UART_FR_TXFF) == 0U;
}

/* Place one known-writable byte into UART0's transmit FIFO. */
void microflow_rp2040_uart0_write(uint8_t value) {
    RP2040_UART0_DR = value;
}

/* Arm TX service only while software has bytes, preventing empty IRQ churn. */
void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled) {
    if (enabled) {
        RP2040_UART0_IMSC |= RP2040_UART_INT_TX;
    } else {
        RP2040_UART0_IMSC &= ~RP2040_UART_INT_TX;
    }
}

/* Put one valid GPIO under SIO control with a low latch before output enable. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin) {
    if (pin >= RP2040_GPIO_COUNT) {
        return;
    }

    RP2040_GPIO_CTRL(pin)   = RP2040_IO_FUNC_SIO;
    RP2040_SIO_GPIO_OUT_CLR = 1U << pin;
    RP2040_SIO_GPIO_OE_SET  = 1U << pin;
}

/* Set or clear one valid SIO output atomically through alias registers. */
void microflow_rp2040_gpio_write(uint32_t pin, bool high) {
    if (pin >= RP2040_GPIO_COUNT) {
        return;
    }

    if (high) {
        RP2040_SIO_GPIO_OUT_SET = 1U << pin;
    } else {
        RP2040_SIO_GPIO_OUT_CLR = 1U << pin;
    }
}

/* -------------------------------------------------------------------------- */
/* WS2812 public functions                                                    */
/* -------------------------------------------------------------------------- */

/*
 * Initialize the onboard WS2812 using RP2040 PIO.
 *
 * This function must be called during board initialization, before
 * FreeRTOS starts scheduling tasks.
 */
void microflow_rp2040_ws2812_initialize(uint32_t pin) {
    if (ws2812_initialized) {
        return;
    }

    if (pin >= NUM_BANK0_GPIOS) {
        return;
    }

    ws2812_configure(pin);

    ws2812_initialized = true;

    /* Allow the LED's reset/latch interval to elapse. */
    busy_wait_us_32(80U);
}

/*
 * Transmit one RGB colour to the WS2812 using PIO.
 *
 * WS2812 expects GRB wire order, with the most significant bit first.
 * The PIO state machine generates the timing independently of FreeRTOS.
 */
void microflow_rp2040_ws2812_write(
    uint32_t pin,
    uint8_t  red,
    uint8_t  green,
    uint8_t  blue) {
    if (!ws2812_initialized) {
        return;
    }

    (void)pin;

    /* Pack the 24-bit GRB value into the upper bits of the FIFO word. */
    const uint32_t grb =
        ((uint32_t)green << 16U) |
        ((uint32_t)red << 8U) |
        (uint32_t)blue;

    /*
     * The PIO output shift register shifts left, so the colour value
     * must occupy the most significant 24 bits of the FIFO word.
     */
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, grb << 8U);

    /*
     * Wait long enough for the previous transmission to complete
     * and the WS2812 to latch the new colour.
     *
     * One 24-bit frame takes approximately 30 us. The additional
     * 80 us reset interval makes this suitable for the single onboard
     * pixel.
     */
    busy_wait_us_32(120U);
}

/* -------------------------------------------------------------------------- */
/* Timing                                                                      */
/* -------------------------------------------------------------------------- */

/* Expose the timer's wrapping low word as the supervisor's monotonic clock. */
uint32_t microflow_rp2040_monotonic_microseconds(void) {
    return RP2040_TIMER_TIMERAWL;
}

/* Busy-wait an explicit millisecond duration for startup and fatal paths. */
void microflow_rp2040_delay_ms(uint32_t milliseconds) {
    while (milliseconds != 0U) {
        const uint32_t start = RP2040_TIMER_TIMERAWL;

        while ((uint32_t)(RP2040_TIMER_TIMERAWL - start) <
               RP2040_MICROSECONDS_PER_MILLISECOND) {
        }

        --milliseconds;
    }
}

void microflow_rp2040_timer_alarm0_event(void) __attribute__((weak));
void microflow_rp2040_uart0_event(void) __attribute__((weak));
void microflow_rp2040_usb_event(void) __attribute__((weak));
/* Default weak timer hook permits SoC-only linking when no board consumes it. */
void microflow_rp2040_timer_alarm0_event(void) {}

/* Default weak UART hook permits SoC-only linking when no board consumes it. */
void microflow_rp2040_uart0_event(void) {}

/* Default weak USB hook permits SoC-only linking when no board consumes it. */
void microflow_rp2040_usb_event(void) {}

/* Acknowledge/disable alarm zero before notifying board-owned supervisor policy. */
void microflow_rp2040_timer0_irq_handler(void) {
    RP2040_TIMER_INTR = RP2040_TIMER_ALARM0_BIT;
    RP2040_TIMER_INTE &= ~RP2040_TIMER_ALARM0_BIT;
    microflow_rp2040_timer_alarm0_event();
}

/* Acknowledge current UART causes before the board drains or fills its FIFOs. */
void microflow_rp2040_uart0_irq_handler(void) {
    const uint32_t status = RP2040_UART0_MIS;
    RP2040_UART0_ICR      = status;
    microflow_rp2040_uart0_event();
}

/* Forward USB controller IRQ notification to an optional board implementation. */
void microflow_rp2040_usbctrl_irq_handler(void) {
    microflow_rp2040_usb_event();
}

/* Close the check/sleep race and wait for either pending work or timer alarm. */
void microflow_rp2040_wait_until(
    const volatile uint32_t *wake_events, bool has_deadline, uint32_t deadline) {
    const uint32_t interrupt_state = microflow_rp2040_interrupt_state();
    microflow_rp2040_interrupt_disable();
    if (has_deadline) {
        RP2040_TIMER_INTR   = RP2040_TIMER_ALARM0_BIT;
        RP2040_TIMER_ALARM0 = deadline;
        RP2040_TIMER_INTE |= RP2040_TIMER_ALARM0_BIT;
        RP2040_NVIC_ICPR = 1U << RP2040_IRQ_TIMER0;
        RP2040_NVIC_ISER = 1U << RP2040_IRQ_TIMER0;
    }
    const bool deadline_arrived = has_deadline &&
                                  ((int32_t)(RP2040_TIMER_TIMERAWL - deadline) >= 0);
    if (((wake_events == NULL) || (*wake_events == 0U)) && !deadline_arrived) {
        /* A masked interrupt becoming pending wakes WFI. Keeping PRIMASK set
         * until after WFI closes the check/sleep race; restore then dispatches. */
        __asm volatile("dsb\n wfi" ::: "memory");
    }
    microflow_rp2040_interrupt_restore(interrupt_state);
}

/* Configure bounded watchdog timing and remember the load used by refreshes. */
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds) {
    if (timeout_microseconds > 0x7FFFFFU) {
        timeout_microseconds = 0x7FFFFFU;
    }
    microflow_rp2040_watchdog_load = timeout_microseconds * 2U;
    RP2040_WATCHDOG_TICK           = RP2040_WATCHDOG_TICK_ENABLE | RP2040_WATCHDOG_TICK_CYCLES;
    RP2040_WATCHDOG_LOAD           = microflow_rp2040_watchdog_load;
    RP2040_WATCHDOG_CTRL           = RP2040_WATCHDOG_CTRL_ENABLE;
}

/* Reload the watchdog after the supervisor demonstrates forward progress. */
void microflow_rp2040_watchdog_service(void) {
    RP2040_WATCHDOG_LOAD = microflow_rp2040_watchdog_load;
}
