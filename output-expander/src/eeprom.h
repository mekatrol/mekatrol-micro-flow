#ifndef OUTPUT_EXPANDER_EEPROM_H
#define OUTPUT_EXPANDER_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Initialize PC1/PC2 and the CH32V003 I2C controller for standard-mode
 * 100 kHz operation.  External pull-ups are required because I2C pins are
 * open-drain: neither the MCU nor EEPROM is allowed to drive a logic high.
 */
void eeprom_init(void);

/*
 * Read an arbitrary byte range.  The driver checks the selected device's
 * capacity before starting the bus transaction.  false means no complete,
 * trustworthy result was obtained (NACK, stuck bus, or bounded timeout).
 */
bool eeprom_read(uint16_t address, uint8_t *data, size_t length);

/*
 * Write an arbitrary byte range and verify every byte by reading it back.
 * Internally this function splits data at the selected EEPROM's page boundary,
 * issues STOP to begin each nonvolatile programming cycle, and ACK-polls until
 * the chip is ready.  This prevents the page-wrap corruption that occurs when
 * a page write crosses the physical device's page boundary.
 */
bool eeprom_write(uint16_t address, const uint8_t *data, size_t length);

#endif
