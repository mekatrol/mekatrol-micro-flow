/* TinyUSB compile-time feature selection for the Pico CDC-only device. */
#ifndef MICROFLOW_TUSB_CONFIG_H
#define MICROFLOW_TUSB_CONFIG_H

/* Compile TinyUSB's RP2040 device-controller implementation. */
#define CFG_TUSB_MCU OPT_MCU_RP2040
/* Port zero operates as a full-speed USB device, never as a host. */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
/* Include the TinyUSB device stack used by the console transport. */
#define CFG_TUD_ENABLED 1
/* Endpoint zero uses the maximum legal full-speed control packet. */
#define CFG_TUD_ENDPOINT0_SIZE 64

/* One CDC ACM interface carries console commands and upload traffic. */
#define CFG_TUD_CDC 1
/* RX buffering absorbs host bursts until the cooperative service runs. */
#define CFG_TUD_CDC_RX_BUFSIZE 128
/* Larger TX buffering accommodates diagnostic response bursts. */
#define CFG_TUD_CDC_TX_BUFSIZE 256
/* CDC bulk transfers use the full-speed maximum packet size. */
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
