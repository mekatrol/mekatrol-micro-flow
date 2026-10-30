#include "config_store.h"

#include "eeprom.h"
#include "output_driver.h"
#include "protocol.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Two fixed-size records provide power-loss recovery.  A replacement is always
 * written to the other slot, so the previously verified record remains usable
 * if power disappears during any page program operation.
 */
#define STORE_SLOT_SIZE 64U
#define STORE_SLOT_A_ADDRESS UINT16_C(0x0000)
#define STORE_SLOT_B_ADDRESS UINT16_C(0x0040)
#define STORE_MAGIC_0 UINT8_C('M')
#define STORE_MAGIC_1 UINT8_C('F')
#define STORE_MAGIC_2 UINT8_C('C')
#define STORE_MAGIC_3 UINT8_C('1')
#define STORE_SCHEMA_VERSION UINT8_C(1)
#define STORE_USED_LENGTH UINT8_C(18)
#define STORE_CRC_OFFSET 60U

/* Explicit on-media offsets.  Reserved bytes are written as 0xff so a future
 * schema can add fields without depending on old compiler structure layout. */
#define OFFSET_MAGIC 0U
#define OFFSET_SCHEMA 4U
#define OFFSET_LENGTH 5U
#define OFFSET_SEQUENCE 6U
#define OFFSET_ADDRESS 10U
#define OFFSET_STARTUP 11U
#define OFFSET_TIMEOUT_LO 12U
#define OFFSET_TIMEOUT_HI 13U
#define OFFSET_TIMEOUT_MASK 14U
#define OFFSET_BRIGHTNESS 15U
#define OFFSET_OUTPUT_DELAY 16U

/*
 * Foreground-owned RAM copy of the currently active record.
 *
 * active_slot is 0 for slot A and 1 for slot B.  active_record_valid is false
 * when boot found no usable record and active_values contains synthesized safe
 * defaults instead.  Keeping that distinction is important: the first commit
 * must write a real record even when its values happen to equal the defaults.
 * No ISR accesses these variables, so volatile or locking is unnecessary.
 */
static config_store_values_t active_values;
static uint32_t active_sequence;
static uint8_t active_slot;
static bool active_record_valid;

/* Decode an explicitly little-endian 32-bit field from EEPROM bytes.  Combining
 * bytes manually avoids assuming the CH32V003 CPU byte order or aligned access. */
static uint32_t load_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
         ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

/* Encode a 32-bit value in the storage format's fixed little-endian order.
 * Each destination is a byte array, so compiler structure padding is irrelevant. */
static void store_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8U);
  bytes[2] = (uint8_t)(value >> 16U);
  bytes[3] = (uint8_t)(value >> 24U);
}

/*
 * Calculate CRC-32/ISO-HDLC over the supplied record prefix.
 *
 * The initial value is all ones, reflected polynomial 0xedb88320 is applied
 * least-significant bit first, and the final value is inverted.  Metadata and
 * reserved bytes are covered too, so a torn page write cannot accidentally
 * combine fields from two record generations and pass validation.
 *
 * mask becomes either 0x00000000 or 0xffffffff.  This applies the polynomial
 * only when the shifted-out CRC bit was one without introducing a branch for
 * every data bit.
 */
static uint32_t record_crc32(const uint8_t *data, size_t length) {
  uint32_t crc = UINT32_C(0xffffffff);
  for (size_t i = 0U; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
      crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
    }
  }
  return ~crc;
}

/* Construct the only settings used when neither EEPROM slot validates.
 * Address 0xff explicitly means uncommissioned; zero masks keep all loads OFF;
 * timeout zero disables fail-safe timing until deliberately configured. */
static config_store_values_t factory_defaults(void) {
  config_store_values_t values = {0};
  values.address = PROTOCOL_UNCOMMISSIONED_ADDRESS;
  values.fields.startup_mask = 0U;
  values.fields.timeout_ms = 0U;
  values.fields.timeout_mask = 0U;
  values.fields.brightness = CONFIG_STORE_DEFAULT_BRIGHTNESS;
  values.output_delay_units = CONFIG_STORE_DEFAULT_OUTPUT_DELAY_UNITS;
  return values;
}

/* Validate fields whose bit patterns have reserved/unsafe meanings.  Address
 * zero is broadcast and may never identify one device.  Address 0xff is valid
 * here because it intentionally represents the factory/uncommissioned state.
 * Output masks, timeout, and brightness permit every value of their types. */
static bool values_valid(const config_store_values_t *values) {
  if (values == NULL || values->address == PROTOCOL_BROADCAST_ADDRESS) {
    return false;
  }
  return values->output_delay_units >=
             OUTPUT_DRIVER_MIN_ACTIVATION_DELAY_UNITS &&
         values->output_delay_units <=
             OUTPUT_DRIVER_MAX_ACTIVATION_DELAY_UNITS;
}

/* Compare semantic fields rather than raw structure bytes.  A compiler may add
 * padding to config_store_values_t, and padding has no persistent meaning. */
static bool values_equal(const config_store_values_t *a,
                         const config_store_values_t *b) {
  return a != NULL && b != NULL && a->address == b->address &&
         config_fields_equal(&a->fields, &b->fields) &&
         a->output_delay_units == b->output_delay_units;
}

/*
 * Build one complete 64-byte on-media record.
 *
 * The buffer is first filled with erased-state 0xff.  Defined fields are then
 * written at fixed offsets and CRC is calculated last over bytes 0..59.  Bytes
 * 17..59 remain reserved but CRC-protected, allowing a future schema to detect
 * whether older firmware really wrote the complete record it understands.
 */
static void encode_record(uint8_t record[STORE_SLOT_SIZE], uint32_t sequence,
                          const config_store_values_t *values) {
  for (size_t i = 0U; i < STORE_SLOT_SIZE; ++i) {
    record[i] = UINT8_C(0xff);
  }
  record[OFFSET_MAGIC + 0U] = STORE_MAGIC_0;
  record[OFFSET_MAGIC + 1U] = STORE_MAGIC_1;
  record[OFFSET_MAGIC + 2U] = STORE_MAGIC_2;
  record[OFFSET_MAGIC + 3U] = STORE_MAGIC_3;
  record[OFFSET_SCHEMA] = STORE_SCHEMA_VERSION;
  record[OFFSET_LENGTH] = STORE_USED_LENGTH;
  store_u32_le(&record[OFFSET_SEQUENCE], sequence);
  record[OFFSET_ADDRESS] = values->address;
  record[OFFSET_STARTUP] = values->fields.startup_mask;
  record[OFFSET_TIMEOUT_LO] = (uint8_t)values->fields.timeout_ms;
  record[OFFSET_TIMEOUT_HI] = (uint8_t)(values->fields.timeout_ms >> 8U);
  record[OFFSET_TIMEOUT_MASK] = values->fields.timeout_mask;
  record[OFFSET_BRIGHTNESS] = values->fields.brightness;
  record[OFFSET_OUTPUT_DELAY] = values->output_delay_units;
  store_u32_le(&record[STORE_CRC_OFFSET],
               record_crc32(record, STORE_CRC_OFFSET));
}

/*
 * Validate and decode one raw slot without partially publishing it.
 *
 * Magic rejects erased/random data, schema and used-length reject incompatible
 * layouts, CRC detects corruption or interrupted writes, and values_valid()
 * rejects a structurally intact record containing unsafe field values.  A
 * temporary candidate is copied to the caller only after every check succeeds.
 */
static bool decode_record(const uint8_t record[STORE_SLOT_SIZE],
                          config_store_values_t *values, uint32_t *sequence) {
  if (record == NULL || values == NULL || sequence == NULL ||
      record[OFFSET_MAGIC + 0U] != STORE_MAGIC_0 ||
      record[OFFSET_MAGIC + 1U] != STORE_MAGIC_1 ||
      record[OFFSET_MAGIC + 2U] != STORE_MAGIC_2 ||
      record[OFFSET_MAGIC + 3U] != STORE_MAGIC_3 ||
      record[OFFSET_SCHEMA] != STORE_SCHEMA_VERSION ||
      record[OFFSET_LENGTH] != STORE_USED_LENGTH ||
      load_u32_le(&record[STORE_CRC_OFFSET]) !=
          record_crc32(record, STORE_CRC_OFFSET)) {
    return false;
  }
  config_store_values_t candidate = {0};
  candidate.address = record[OFFSET_ADDRESS];
  candidate.fields.startup_mask = record[OFFSET_STARTUP];
  candidate.fields.timeout_ms =
      (uint16_t)((uint16_t)record[OFFSET_TIMEOUT_LO] |
                 ((uint16_t)record[OFFSET_TIMEOUT_HI] << 8U));
  candidate.fields.timeout_mask = record[OFFSET_TIMEOUT_MASK];
  candidate.fields.brightness = record[OFFSET_BRIGHTNESS];
  candidate.output_delay_units = record[OFFSET_OUTPUT_DELAY];
  if (!values_valid(&candidate)) {
    return false;
  }
  *values = candidate;
  *sequence = load_u32_le(&record[OFFSET_SEQUENCE]);
  return true;
}

/* Signed subtraction implements modulo-2^32 sequence ordering.  It remains
 * unambiguous because only two adjacent generations can occupy the slots. */
static bool sequence_newer(uint32_t candidate, uint32_t reference) {
  return (int32_t)(candidate - reference) > 0;
}

config_store_load_result_t config_store_init(void) {
  /* Read both slots independently.  A bus failure or corrupt slot must not hide
   * a valid peer slot left from the preceding successful transaction. */
  uint8_t a[STORE_SLOT_SIZE];
  uint8_t b[STORE_SLOT_SIZE];
  config_store_values_t av = {0};
  config_store_values_t bv = {0};
  uint32_t as = 0U;
  uint32_t bs = 0U;
  const bool read_a = eeprom_read(STORE_SLOT_A_ADDRESS, a, sizeof(a));
  const bool read_b = eeprom_read(STORE_SLOT_B_ADDRESS, b, sizeof(b));
  const bool valid_a = read_a && decode_record(a, &av, &as);
  const bool valid_b = read_b && decode_record(b, &bv, &bs);

  /* Prefer A when it is the only valid slot or when B is not newer.  Equal
   * sequences should not normally occur, but choosing deterministically avoids
   * boot behavior depending on incidental read order after external cloning. */
  if (valid_a && (!valid_b || !sequence_newer(bs, as))) {
    active_values = av;
    active_sequence = as;
    active_slot = 0U;
    active_record_valid = true;
    return CONFIG_STORE_LOADED;
  }

  /* If A was not selected and B validates, B is either the sole valid record
   * or the wrap-aware newer generation. */
  if (valid_b) {
    active_values = bv;
    active_sequence = bs;
    active_slot = 1U;
    active_record_valid = true;
    return CONFIG_STORE_LOADED;
  }

  /* No slot is trustworthy.  Publish safe RAM defaults but mark them as not
   * backed by a record.  active_slot=1 makes the first commit target slot A.
   * Distinguish readable blank/corrupt storage from an actual I2C read failure
   * so higher layers can expose a storage diagnostic if desired. */
  active_values = factory_defaults();
  active_sequence = 0U;
  active_slot = 1U; /* first commit targets slot A */
  active_record_valid = false;
  return (read_a && read_b) ? CONFIG_STORE_DEFAULTED : CONFIG_STORE_IO_ERROR;
}

/* Return a read-only pointer to the foreground-owned active RAM settings.  The
 * address remains valid for program life; a successful commit updates contents. */
const config_store_values_t *config_store_get(void) { return &active_values; }

/*
 * Transactionally replace the persistent configuration.
 *
 * Invalid candidates are rejected before I2C activity.  An unchanged verified
 * candidate returns success without consuming an EEPROM erase/program cycle.
 * Otherwise the inactive slot receives sequence+1.  The complete slot is read
 * back, structurally decoded, and semantically compared.  Only then are the RAM
 * active values and slot identity changed.  Any failure therefore leaves the
 * previous verified record and runtime configuration active.
 */
bool config_store_commit(const config_store_values_t *values) {
  if (!values_valid(values)) {
    return false;
  }
  if (active_record_valid && values_equal(values, &active_values)) {
    return true;
  }

  /* XOR with one alternates 0<->1.  When boot had no valid record, initialization
   * deliberately set active_slot to 1 so this selects slot A at address zero. */
  const uint8_t target_slot = (uint8_t)(active_slot ^ 1U);
  const uint16_t target_address =
      target_slot == 0U ? STORE_SLOT_A_ADDRESS : STORE_SLOT_B_ADDRESS;
  const uint32_t next_sequence = active_sequence + 1U;
  uint8_t record[STORE_SLOT_SIZE];
  uint8_t verify[STORE_SLOT_SIZE];
  config_store_values_t verified_values = {0};
  uint32_t verified_sequence = 0U;
  encode_record(record, next_sequence, values);

  /* eeprom_write() already performs byte readback, but decoding a separately
   * read slot here verifies the exact persistent object that boot will later
   * consume, including magic, schema, sequence, reserved bytes, and CRC. */
  if (!eeprom_write(target_address, record, sizeof(record)) ||
      !eeprom_read(target_address, verify, sizeof(verify)) ||
      !decode_record(verify, &verified_values, &verified_sequence) ||
      verified_sequence != next_sequence ||
      !values_equal(&verified_values, values)) {
    return false;
  }

  /* Publish atomically at the C object level only after all verification.  No
   * earlier statement changes the configuration observed by command handling. */
  active_values = verified_values;
  active_sequence = verified_sequence;
  active_slot = target_slot;
  active_record_valid = true;
  return true;
}

/* Factory reset is a normal transactional commit, not a bulk erase.  Therefore
 * power loss retains either the old valid settings or a complete defaults record,
 * and successful reset has exactly the same readback guarantees as SET_CONFIG. */
bool config_store_factory_reset(void) {
  const config_store_values_t defaults = factory_defaults();
  return config_store_commit(&defaults);
}
