
#include "rp2_ws2812.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "ws2812.pio.h"

/*
 * The WS2812 protocol transmits 800,000 bits per second.
 *
 * Our PIO program takes exactly 10 cycles per bit.
 */
#define WS2812_BIT_RATE       800000.0f
#define WS2812_CYCLES_PER_BIT 10.0f

/*
 * Number of active outputs using each PIO block.
 *
 * The program is loaded once per block and shared by all
 * state machines allocated for this driver.
 */
static uint8_t pio_users[NUM_PIOS];

/*
 * Instruction-memory offset for the shared PIO program.
 */
static uint pio_offsets[NUM_PIOS];

/*
 * Return the PIO instance associated with an index.
 */
static PIO rp2_ws2812_get_pio(uint index) {
    return pio_get_instance(index);
}

/*
 * Initialize one independent WS2812 output.
 */
bool rp2_ws2812_init(rp2_ws2812_strip_t *strip, uint pin) {
    if (strip == NULL ||
        strip->initialized ||
        pin >= NUM_BANK0_GPIOS) {
        return false;
    }

    /*
     * Search every PIO block for a free state machine.
     *
     * RP2040: 2 PIO blocks.
     * RP2350: 3 PIO blocks.
     */
    for (uint i = 0; i < NUM_PIOS; ++i) {
        PIO pio = rp2_ws2812_get_pio(i);

        /*
         * Find a free state machine without claiming one
         * that is already in use.
         */
        int sm = pio_claim_unused_sm(pio, false);

        if (sm < 0) {
            continue;
        }

        /*
         * If this is the first WS2812 output on this PIO,
         * load the four-instruction program into memory.
         *
         * Otherwise, reuse the previously loaded program.
         */
        if (pio_users[i] == 0) {
            if (!pio_can_add_program(pio, &ws2812_program)) {
                pio_sm_unclaim(pio, (uint)sm);
                continue;
            }

            pio_offsets[i] =
                pio_add_program(pio, &ws2812_program);
        }

        uint offset = pio_offsets[i];

        /*
         * Obtain the default state machine configuration
         * generated from ws2812.pio.
         */
        pio_sm_config config =
            ws2812_program_get_default_config(offset);

        /*
         * Assign the side-set output to the requested GPIO.
         */
        sm_config_set_sideset_pins(&config, pin);

        /*
         * Shift MSB first.
         *
         * false = shift left.
         * true  = enable autopull.
         * 24    = pull a new word after 24 bits.
         */
        sm_config_set_out_shift(
            &config, false, true, 24U);

        /*
         * Combine TX and RX FIFOs into one 8-word TX FIFO.
         */
        sm_config_set_fifo_join(
            &config, PIO_FIFO_JOIN_TX);

        /*
         * Configure the state machine clock to 8 MHz.
         *
         * 800,000 bits/s x 10 cycles/bit = 8 MHz.
         */
        const float divider =
            (float)clock_get_hz(clk_sys) /
            (WS2812_BIT_RATE * WS2812_CYCLES_PER_BIT);

        sm_config_set_clkdiv(&config, divider);

        /*
         * Hand control of the GPIO to the selected PIO.
         */
        pio_gpio_init(pio, pin);

        /*
         * Configure the GPIO as an output.
         */
        pio_sm_set_consecutive_pindirs(
            pio, (uint)sm, pin, 1, true);

        /*
         * Initialize the state machine at the shared
         * program's instruction-memory offset.
         */
        pio_sm_init(pio, (uint)sm, offset, &config);

        /*
         * Start executing the PIO program.
         */
        pio_sm_set_enabled(pio, (uint)sm, true);

        /*
         * Store this output's allocated resources.
         */
        strip->pio         = pio;
        strip->sm          = (uint)sm;
        strip->pin         = pin;
        strip->initialized = true;

        ++pio_users[i];

        return true;
    }

    return false;
}

/*
 * Send one RGB colour to the strip.
 */
bool rp2_ws2812_write_rgb(
    rp2_ws2812_strip_t *strip,
    uint8_t             red,
    uint8_t             green,
    uint8_t             blue) {
    if (strip == NULL || !strip->initialized) {
        return false;
    }

    /*
     * WS2812 expects GRB ordering:
     *
     * Bits 23..16 = GREEN
     * Bits 15..8  = RED
     * Bits 7..0   = BLUE
     */
    uint32_t grb =
        ((uint32_t)green << 16U) |
        ((uint32_t)red << 8U) |
        (uint32_t)blue;

    /*
     * The OSR shifts left, so position the 24 colour bits
     * in bits 31..8 of the FIFO word.
     *
     * pio_sm_put_blocking() waits if the TX FIFO is full.
     */
    pio_sm_put_blocking(
        strip->pio,
        strip->sm,
        grb << 8U);

    return true;
}

/*
 * Send multiple RGB pixels to the same strip.
 */
bool rp2_ws2812_write_pixels(
    rp2_ws2812_strip_t *strip,
    const uint32_t     *pixels,
    size_t              count) {
    if (strip == NULL ||
        !strip->initialized ||
        (count != 0 && pixels == NULL)) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        uint32_t rgb = pixels[i];

        uint8_t red   = (rgb >> 16U) & 0xFFU;
        uint8_t green = (rgb >> 8U) & 0xFFU;
        uint8_t blue  = rgb & 0xFFU;

        if (!rp2_ws2812_write_rgb(
                strip, red, green, blue)) {
            return false;
        }
    }

    return true;
}

/*
 * Release an initialized WS2812 output.
 *
 * Caller must ensure transmission has finished.
 */
void rp2_ws2812_deinit(rp2_ws2812_strip_t *strip) {
    if (strip == NULL || !strip->initialized) {
        return;
    }

    PIO  pio   = strip->pio;
    uint sm    = strip->sm;
    uint index = pio_get_index(pio);

    /*
     * Stop the state machine.
     */
    pio_sm_set_enabled(pio, sm, false);

    /*
     * Release the state machine so another driver can use it.
     */
    pio_sm_unclaim(pio, sm);

    /*
     * Track how many outputs still use the shared program.
     */
    --pio_users[index];

    /*
     * When the final user is released, free the program's
     * instruction memory.
     */
    if (pio_users[index] == 0) {
        pio_remove_program(
            pio,
            &ws2812_program,
            pio_offsets[index]);
    }

    strip->initialized = false;
}
