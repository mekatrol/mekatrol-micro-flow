#include "config_store.h"
#include "eeprom.h"
#include "protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t memory[256];
static bool writes_allowed;
static unsigned int writes;

bool eeprom_read(uint16_t address, uint8_t *data, size_t length) {
  if ((size_t)address + length > sizeof(memory)) {
    return false;
  }
  memcpy(data, &memory[address], length);
  return true;
}

bool eeprom_write(uint16_t address, const uint8_t *data, size_t length) {
  if (!writes_allowed || (size_t)address + length > sizeof(memory)) {
    return false;
  }
  memcpy(&memory[address], data, length);
  ++writes;
  return true;
}

int main(void) {
  /* An erased part must never interpret 0xff fields as commissioned settings. */
  memset(memory, 0xff, sizeof(memory));
  writes_allowed = true;
  writes = 0U;
  assert(config_store_init() == CONFIG_STORE_DEFAULTED);
  assert(config_store_get()->address == PROTOCOL_UNCOMMISSIONED_ADDRESS);
  assert(config_store_get()->fields.startup_mask == 0U);
  assert(config_store_get()->output_delay_units == 5U);

  config_store_values_t first = *config_store_get();
  first.address = 0x34U;
  first.fields.startup_mask = 0xa5U;
  first.fields.timeout_ms = 1234U;
  assert(config_store_commit(&first));
  assert(writes == 1U);
  assert(config_store_commit(&first));
  assert(writes == 1U); /* unchanged commits consume no endurance */

  /* A failed replacement must not publish partially requested settings. */
  config_store_values_t failed = first;
  failed.address = 0x35U;
  writes_allowed = false;
  assert(!config_store_commit(&failed));
  assert(config_store_get()->address == 0x34U);

  writes_allowed = true;
  assert(config_store_factory_reset());
  assert(config_store_get()->address == PROTOCOL_UNCOMMISSIONED_ADDRESS);
  assert(config_store_get()->fields.timeout_ms == 0U);

  /* Reboot selection must recover the newest verified slot. */
  assert(config_store_init() == CONFIG_STORE_LOADED);
  assert(config_store_get()->address == PROTOCOL_UNCOMMISSIONED_ADDRESS);
  return 0;
}
