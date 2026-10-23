#ifndef MICROFLOW_SOC_RP2040_H
#define MICROFLOW_SOC_RP2040_H

#include <stdbool.h>
#include <stdint.h>

void microflow_rp2040_initialize(void);
uint32_t microflow_rp2040_monotonic_microseconds(void);
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);
void microflow_rp2040_wait_until(
    const volatile uint32_t *wake_events, bool has_deadline, uint32_t deadline);
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds);
void microflow_rp2040_watchdog_service(void);
uint32_t microflow_rp2040_interrupt_lock(void);
void microflow_rp2040_interrupt_unlock(uint32_t state);
void microflow_rp2040_timer_alarm0_event(void);
void microflow_rp2040_uart0_event(void);
void microflow_rp2040_usb_event(void);
void microflow_rp2040_timer0_irq_handler(void);
void microflow_rp2040_uart0_irq_handler(void);
void microflow_rp2040_usbctrl_irq_handler(void);

#endif
