# Waveshare RP2040 Zero UART upload protocol

UART0 uses GPIO0 (TX), GPIO1 (RX), 115200 baud, eight data bits, no parity,
and one stop bit. Multi-byte integers are little-endian.

Each frame is `MFLO`, protocol version (one byte), message type (one byte),
payload length (two bytes), sequence (four bytes), CRC-32 (four bytes), then
payload. CRC-32 is the standard reflected polynomial `0xedb88320`, initialized
and finalized with all bits set, over the fields from version through sequence
followed by the payload. The maximum payload is 260 bytes.

Request types are probe (1), begin (3), data (4), complete (5), and abort (6).
Responses use device information (2), the request type for acknowledgements,
validation result (7), activation result (8), or error (127). Result payloads
contain one status byte. Sequences begin at zero after reset and advance once
for every checksum-valid, correctly sequenced request, including requests with
semantic errors. A checksum failure does not consume a sequence number.

Begin carries image length and image CRC-32 as two 32-bit words. Data carries
the required byte offset followed by one or more image bytes. Chunks must be
contiguous. Complete has no payload. The supervisor validates the whole staged
image before activation. Abort and a one-second partial-frame/upload timeout
discard staging without changing the active image.

The current RAM image is at most 2048 bytes. It has a 16-byte `MFFL` envelope:
magic, version 1, flags 0, header size 16, total size, then CRC-32 of the bytes
after the header. Those payload bytes are opaque until the portable engine
defines its serialized flow representation. Thus activation currently means a
validated atomic replacement of the board's active RAM image, not execution.

The host utility requires Python 3 and pyserial:

```text
python tools/microflow_uart_upload.py /dev/ttyUSB0
python tools/microflow_uart_upload.py /dev/ttyUSB0 path/to/flow-payload.bin
```

Use a 3.3 V UART adapter and connect grounds. Do not attach a 5 V UART signal.
