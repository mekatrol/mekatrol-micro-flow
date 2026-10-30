#ifndef OUTPUT_EXPANDER_CONFIG_STORE_H
#define OUTPUT_EXPANDER_CONFIG_STORE_H

#include "config_fields.h"

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_STORE_DEFAULT_BRIGHTNESS UINT8_C(30)
#define CONFIG_STORE_DEFAULT_OUTPUT_DELAY_UNITS UINT8_C(5)

/* All persistent user settings.  This object is serialized field-by-field;
 * compiler padding and CPU byte order are never stored in EEPROM. */
typedef struct {
  uint8_t address;
  config_fields_t fields;
  uint8_t output_delay_units;
} config_store_values_t;

typedef enum {
  CONFIG_STORE_LOADED = 0,
  CONFIG_STORE_DEFAULTED,
  CONFIG_STORE_IO_ERROR
} config_store_load_result_t;

/* Read both redundant records, validate CRC/schema/ranges, and select the
 * newest valid sequence.  Blank, interrupted, or corrupt storage returns safe
 * defaults rather than treating erased 0xff bytes as configured settings. */
config_store_load_result_t config_store_init(void);

const config_store_values_t *config_store_get(void);

/* Commit complete replacement values into the inactive slot, verify them, and
 * publish to RAM only after verification.  An unchanged value causes no write,
 * preserving EEPROM endurance. */
bool config_store_commit(const config_store_values_t *values);

/* Restore commissioning defaults through the same power-fail-safe transaction.
 * In particular, the address becomes the uncommissioned sentinel 0xff. */
bool config_store_factory_reset(void);

#endif
