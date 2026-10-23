#include "tusb.h"

#include <stddef.h>
#include <stdint.h>

#define USB_VID 0x1209U
#define USB_PID 0x2040U
#define USB_CONFIGURATION_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, USB_CONFIGURATION_TOTAL_LENGTH, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)};

static const char *const string_descriptors[] = {
    "", "Mekatrol", "Micro Flow RP2040 Zero", "0001", "Micro Flow Console"};
static uint16_t string_descriptor[32];

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
  (void)language_id;
  if (index == 0U) {
    string_descriptor[1] = 0x0409U;
    string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8U) | 4U);
    return string_descriptor;
  }
  if (index >= (sizeof(string_descriptors) / sizeof(string_descriptors[0]))) return NULL;
  const char *source = string_descriptors[index];
  size_t length = 0U;
  while ((source[length] != '\0') && (length < 31U)) {
    string_descriptor[1U + length] = (uint16_t)(uint8_t)source[length];
    ++length;
  }
  string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8U) | (2U * length + 2U));
  return string_descriptor;
}
