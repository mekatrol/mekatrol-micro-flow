/* TinyUSB compile-time feature selection for the Pico CDC-only device. */
#ifndef MICROFLOW_TUSB_CONFIG_H
#define MICROFLOW_TUSB_CONFIG_H

/* Compile the RP2040 device controller because this target uses its integrated USB hardware. */
#define CFG_TUSB_MCU OPT_MCU_RP2040
/* Port zero operates as a full-speed USB device, never as a host. */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
/* Include the TinyUSB device stack used by the console transport. */
#define CFG_TUD_ENABLED 1
/* Use the maximum legal control packet so full-speed enumeration needs fewer transactions. */
#define CFG_TUD_ENDPOINT0_SIZE 64

/* Enable one CDC interface so commands and uploads share a single serial abstraction. */
#define CFG_TUD_CDC 1
/* Buffer RX data so host bursts survive until the cooperative USB service runs. */
#define CFG_TUD_CDC_RX_BUFSIZE 128
/* Size TX for diagnostic bursts so delayed host reads do not immediately drop replies. */
#define CFG_TUD_CDC_TX_BUFSIZE 256
/* Use maximum full-speed bulk packets so queued CDC data needs fewer transfers. */
#define CFG_TUD_CDC_EP_BUFSIZE 64
/* Mass storage is disabled because firmware exposes no block device. */
#define CFG_TUD_MSC 0
/* HID is disabled because all interaction uses CDC. */
#define CFG_TUD_HID 0
/* MIDI is disabled because it is outside the board capability set. */
#define CFG_TUD_MIDI 0
/* Vendor-specific interfaces are disabled to keep descriptors and RAM small. */
#define CFG_TUD_VENDOR 0

#endif
