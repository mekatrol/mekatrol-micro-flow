
#ifndef OUTPUT_EXPANDER_RESPONSE_TX_H
#define OUTPUT_EXPANDER_RESPONSE_TX_H

#include "protocol.h"

#include <stdbool.h>

/*
 * Transmit one complete protocol response.
 *
 * WHAT:
 * Validate the outgoing packet, encode it as a
 * version-one CRC-protected COBS frame, and pass
 * the complete frame to the existing USART driver.
 *
 * WHY:
 * The foreground command processor produces decoded
 * response packets, while USART1 requires wire bytes.
 *
 * Keeping this boundary separate makes framing and
 * transmission behavior independently testable.
 *
 * Only TYPE=RESPONSE and TYPE=ERROR packets addressed
 * to 0x01..0xFE may be transmitted.
 *
 * The function never transmits requests, broadcasts,
 * or packets using reserved destination 0xFF.
 *
 * Return true only when validation and encoding succeed
 * and the blocking USART transmission returns.
 *
 * Return false without transmitting when validation
 * or encoding fails.
 *
 * IMPORTANT:
 * A true result means the bytes were submitted to the
 * USART driver and its blocking function returned.
 * It does not establish that the master received or
 * acknowledged the response.
 *
 * Hardware prerequisites:
 * - hal_init() and usart_init() have completed.
 * - The function is called from foreground context.
 * - No other foreground transmission is in progress.
 * - The supplied packet remains valid during the call.
 *
 * This function allocates no heap memory and does not
 * change output states or persistent configuration.
 */
bool response_tx_send(const protocol_packet_t *packet);

#endif
