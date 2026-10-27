# Output expander protocol

Modbus RTU uses 19,200 bit/s, 8E1. Multi-byte Modbus fields are big-endian; CRC16 is low byte first. At exactly 19,200 bit/s, t1.5 is 860 us and t3.5 is 2006 us. Broadcast address 0 may write outputs but never receives a response.

## Data model

- Coils 0–7: output channels 1–8, LSB first, `1=ON`.
- Holding 0: slave ID (`1..247`, or internal `0xFF` unconfigured); normal writes forbidden.
- Holding 1: startup output mask.
- Holding 2: communications timeout milliseconds; zero disables it.
- Holding 3: timeout output mask.
- Holding 4: LED brightness, 0–255.
- Input 0: firmware major in the high byte, minor in the low byte.
- Input 1: current output mask.
- Input 2: status flags: bit 0 configured, bit 1 communications timeout,
  bit 2 UART receive error, bit 3 persistent-configuration fault. Unallocated
  bits are zero and must be ignored by a controller.
- Input 3–8: the full 96-bit UID as six big-endian 16-bit words in HAL byte order.

Supported standard functions are 01, 03, 04, 05 and 0F. Function 06 remains disabled until persistent commit semantics are hardware-validated. Responses use exceptions 01–04 as applicable. The implementation caps ADUs at 128 bytes and rejects larger frames.

## Commissioning function 41

The safe first release supports isolated-device assignment only. A controller must ensure exactly one unconfigured device is attached.

`ASSIGN_UID` is a broadcast request with payload `03 | UID[12] | NEW_ID`, covered by the normal RTU CRC. A configured or unconfigured device whose entire 96-bit UID matches may commit an ID from 1 through 247. This allows the board owner to recover or reorganise bus addresses without erasing the device. It sends no broadcast response. The controller then addresses `NEW_ID`, reads input registers 3–8, and requires an exact UID match.

Prefix discovery and unsolicited slot responses are not enabled: collision detection cannot be guaranteed without a separately specified and tested master timing protocol. Address assignment and readdressing require the complete UID, limiting the command to the intended board. CRC detects transmission errors but does not authenticate a commissioning command.
