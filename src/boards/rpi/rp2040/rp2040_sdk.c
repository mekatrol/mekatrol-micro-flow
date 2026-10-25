/* Implement project-owned RP2040 primitives with the Pico SDK so SDK types stay at this boundary. */
#include "rp2040_sdk.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

/* UART0 is the dedicated interrupt-driven upload and diagnostic port. */
#define MICROFLOW_UART uart0
/* IRQ identity stays beside the UART selection to prevent mismatched wiring. */
#define MICROFLOW_UART_IRQ UART0_IRQ
/* GPIO0 carries UART0 transmit on the maintained Pico board mapping. */
#define MICROFLOW_UART_TX_PIN 0U
/* GPIO1 carries UART0 receive on the maintained Pico board mapping. */
#define MICROFLOW_UART_RX_PIN 1U

/* Tracks TX policy because the SDK exposes enable state only indirectly. */
static bool uart_transmit_interrupt_enabled;

void microflow_rp2040_initialize(void) {
}

/* Forward UART0 IRQ work to the board composition layer that owns its queues. */
static void uart0_irq_handler(void) {
    microflow_rp2040_uart0_event();
}

/* Expose the SDK's wrapping microsecond timer using a project-owned scalar type. */
uint32_t microflow_rp2040_monotonic_microseconds(void) {
    return time_us_32();
}

/* Claim a physical GPIO and enable its output driver for board-controlled use. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin) {
    /* GPIO ownership and direction are established before application use. */
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
}

/* Claim a physical GPIO as an unbiased high-impedance input. */
void microflow_rp2040_gpio_input_initialize(uint32_t pin) {
    /* Electrical bias remains an explicit board-level responsibility. */
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_set_dir(pin, GPIO_IN);
}

/* Sample one physical GPIO without exposing the SDK's GPIO API to board policy. */
bool microflow_rp2040_gpio_read(uint32_t pin) {
    return gpio_get(pin);
}

/* Set the output latch of one physical GPIO through the project boundary. */
void microflow_rp2040_gpio_write(uint32_t pin, bool high) {
    gpio_put(pin, high);
}

/*
 * The Waveshare RP2040-Zero has one onboard WS2812 pixel.
 *
 * PIO0 state machine 0 is reserved for this driver. No other peripheral
 * may claim that state machine while the driver is active.
 */
static PIO  ws2812_pio = pio0;
static uint ws2812_sm;
static bool ws2812_initialized = false;

/*
 * Configure the WS2812 state machine for an 800 kHz serial bitstream.
 *
 * The PIO program uses ten state-machine cycles per transmitted bit.
 */
static void ws2812_configure(uint32_t pin) {
    const uint offset = pio_add_program(ws2812_pio, &ws2812_program);

    pio_sm_config config = ws2812_program_get_default_config(offset);

    /* Drive the WS2812 data signal through the PIO side-set pin. */
    sm_config_set_sideset_pins(&config, pin);

    /* Shift the most significant bit first. */
    sm_config_set_out_shift(&config, false, true, 24U);

    /* Join both FIFOs into one eight-word transmit FIFO. */
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    /*
     * WS2812 requires 800,000 bits per second.
     * Each bit occupies ten PIO clock cycles.
     */
    const float clock_divider =
        (float)clock_get_hz(clk_sys) / (800000.0f * 10.0f);

    sm_config_set_clkdiv(&config, clock_divider);

    /* Hand ownership of the LED data pin to PIO. */
    pio_gpio_init(ws2812_pio, pin);

    /* Configure the selected pin as an output. */
    pio_sm_set_consecutive_pindirs(
        ws2812_pio, ws2812_sm, pin, 1U, true);

    /* Initialize and start the state machine. */
    pio_sm_init(ws2812_pio, ws2812_sm, offset, &config);
    pio_sm_set_enabled(ws2812_pio, ws2812_sm, true);
}

/* Claim and initialize a PIO state machine for the onboard WS2812. */
void microflow_rp2040_ws2812_initialize(uint32_t pin) {
    if (ws2812_initialized || pin >= NUM_BANK0_GPIOS) {
        return;
    }

    /* Select PIO0 for the onboard RGB LED. */
    ws2812_pio = pio0;

    /*
     * Claim an available state machine rather than assuming SM0 is free.
     *
     * The required=true argument causes an assertion/panic if all state
     * machines are already allocated.
     */
    ws2812_sm = pio_claim_unused_sm(ws2812_pio, true);

    /* Load the WS2812 program and configure the claimed state machine. */
    ws2812_configure(pin);

    /* Mark the driver ready only after initialization completes. */
    ws2812_initialized = true;

    /* Allow the LED's reset/latch interval before the first update. */
    busy_wait_us_32(80U);
}

/* Transmit one RGB colour through PIO using WS2812 GRB ordering. */
void microflow_rp2040_ws2812_write(
    uint32_t pin,
    uint8_t  red,
    uint8_t  green,
    uint8_t  blue) {
    if (!ws2812_initialized || pin >= NUM_BANK0_GPIOS) {
        return;
    }

    /* WS2812 requires green, red, then blue, most significant bit first. */
    const uint32_t grb =
        ((uint32_t)green << 16U) |
        ((uint32_t)red << 8U) |
        (uint32_t)blue;

    /*
     * The PIO output shift register shifts left, so position the
     * 24 colour bits in the upper 24 bits of the FIFO word.
     */
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, grb << 8U);

    /*
     * Allow the 24-bit frame to finish and the LED to latch.
     * This is sufficient for the single onboard WS2812 at 800 kHz.
     */
    busy_wait_us_32(120U);
}

/* Provide an explicit millisecond blocking delay for startup/fatal paths only. */
void microflow_rp2040_delay_ms(uint32_t milliseconds) {
    sleep_ms(milliseconds);
}

/* Start the hardware watchdog with a conservatively rounded timeout. */
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds) {
    /* Round up because the SDK accepts integer milliseconds. */
    watchdog_enable((timeout_microseconds + 999U) / 1000U, true);
}

/* Refresh the watchdog after a completed supervisor pass proves forward progress. */
void microflow_rp2040_watchdog_service(void) {
    watchdog_update();
}

/* Force a controlled chip reset through the watchdog and never return. */
void microflow_rp2040_reboot(void) {
    watchdog_reboot(0U, 0U, 0U);
    for (;;) {
        tight_loop_contents();
    }
}

/* Configure UART0 pins, framing, and receive IRQs for the upload transport. */
void microflow_rp2040_uart0_initialize(uint32_t baud_rate) {
    /* Configure 8-N-1 and enable RX interrupts; TX starts demand-disabled. */
    (void)uart_init(MICROFLOW_UART, baud_rate);
    gpio_set_function(MICROFLOW_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MICROFLOW_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(MICROFLOW_UART, false, false);
    uart_set_format(MICROFLOW_UART, 8U, 1U, UART_PARITY_NONE);
    uart_set_fifo_enabled(MICROFLOW_UART, true);
    irq_set_exclusive_handler(MICROFLOW_UART_IRQ, uart0_irq_handler);
    irq_set_enabled(MICROFLOW_UART_IRQ, true);
    uart_set_irq_enables(MICROFLOW_UART, true, false);
}

/* Tell the ISR whether reading UART0 can complete without blocking. */
bool microflow_rp2040_uart0_read_ready(void) {
    return uart_is_readable(MICROFLOW_UART);
}

/* Remove one known-available UART byte for transfer into the RX software ring. */
uint8_t microflow_rp2040_uart0_read(void) {
    return (uint8_t)uart_getc(MICROFLOW_UART);
}

/* Tell the ISR whether UART0 can accept a byte without blocking. */
bool microflow_rp2040_uart0_write_ready(void) {
    return uart_is_writable(MICROFLOW_UART);
}

/* Place one known-writable byte into UART0 without text translation. */
void microflow_rp2040_uart0_write(uint8_t value) {
    uart_putc_raw(MICROFLOW_UART, (char)value);
}

/* Enable TX IRQs only while software has queued bytes, avoiding interrupt storms. */
void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled) {
    /* RX remains enabled while TX follows whether queued output exists. */
    uart_transmit_interrupt_enabled = enabled;
    uart_set_irq_enables(MICROFLOW_UART, true, uart_transmit_interrupt_enabled);
}

/* Save and disable interrupts for a short board-owned shared-state operation. */
uint32_t microflow_rp2040_interrupt_lock(void) {
    return save_and_disable_interrupts();
}

/* Restore the caller's prior interrupt state rather than blindly enabling IRQs. */
void microflow_rp2040_interrupt_unlock(uint32_t state) {
    restore_interrupts(state);
}

/* Return the configured RP2040 system clock in hertz. */
uint32_t microflow_rp2040_system_clock_hz(void) {
    return clock_get_hz(clk_sys);
}
