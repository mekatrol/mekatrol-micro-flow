# EEPROM Configuration Debugging Runbook

## Purpose

This document records the complete context needed to resume debugging the
output expander's external EEPROM without relying on chat history.

The development board currently has an AT24C256 connected to the CH32V003:

| Signal | CH32V003 pin | Function |
|---|---:|---|
| SDA | PC1 | I2C serial data |
| SCL | PC2 | I2C serial clock |

The intended production part is the XBLW 24C02S. The firmware selects EEPROM
geometry at compile time rather than probing the device at runtime.

| CMake profile value | Device | Capacity | Internal address bytes | Page size |
|---:|---|---:|---:|---:|
| `1` | AT24C256 development device | 32,768 bytes | 2 | 64 bytes |
| `2` | XBLW 24C02S production device | 256 bytes | 1 | 16 bytes |

The default profile is `1`. A production build is selected with:

```sh
cmake --preset ch32v003-release -DOUTPUT_EXPANDER_EEPROM_PROFILE=2
cmake --build --preset ch32v003-release
```

Both devices use seven-bit I2C address `0x50` in this design.

## Relevant source files

| File | Responsibility |
|---|---|
| `src/eeprom_profile.h` | Compile-time capacity, address-width, and page-size selection |
| `src/eeprom.c` | CH32V003 I2C controller and physical EEPROM read/write implementation |
| `src/eeprom.h` | Physical EEPROM interface |
| `src/config_store.c` | Dual-slot records, CRC-32, sequence selection, defaults, and transactional commits |
| `src/config_store.h` | Persistent configuration types and interface |
| `src/comms_app.c` | RS485 configuration commands and persistent address assignment |
| `src/registers.h` | CH32V003 register addresses and bit definitions |
| `tools/device_tool.py` | Host-side scan, assignment, configuration, and factory-reset commands |

## Persistent record design

The first 128 EEPROM bytes contain two independent 64-byte records:

| EEPROM range | Purpose |
|---|---|
| `0x0000..0x003f` | Slot A |
| `0x0040..0x007f` | Slot B |

Each record contains:

- Magic bytes `MFC1`.
- Schema version.
- Used-length field.
- 32-bit sequence number.
- Commissioned RS485 address.
- Startup output mask.
- Communications timeout.
- Timeout output mask.
- LED brightness.
- Output activation delay.
- Reserved bytes.
- CRC-32 over bytes `0..59`.

A commit writes the inactive slot, waits for EEPROM programming to complete,
reads every byte back, validates the complete record, and only then publishes
the new values in RAM. A failed commit leaves the previous settings active.

## Expected blank-device behavior

At boot, `config_store_init()` reads both slots. A blank EEPROM normally
contains `0xff`, so both physical reads should succeed but neither record should
validate.

The loader return values are:

| Numeric value | Enum | Meaning |
|---:|---|---|
| `0` | `CONFIG_STORE_LOADED` | At least one valid EEPROM record was loaded |
| `1` | `CONFIG_STORE_DEFAULTED` | EEPROM was readable, but no slot was valid; safe defaults were selected |
| `2` | `CONFIG_STORE_IO_ERROR` | One or both physical EEPROM reads failed |

Safe blank-device defaults are:

```text
active_record_valid = false
active_slot = 1
active_sequence = 0
address = 0xff
startup_mask = 0x00
timeout_ms = 0
timeout_mask = 0x00
brightness = 30
output_delay_units = 5
```

`active_slot` is deliberately set to `1` when no record is valid so that the
first commit targets slot A (`active_slot ^ 1` gives zero).

## Build configuration

The unoptimised debug image no longer fits in the CH32V003's 16 KiB Flash.
The embedded debug preset therefore uses:

```text
-Og -g3
```

This retains DWARF source information while applying debug-friendly
optimisation. The verified sizes at the time this document was written were:

| Build | Flash | Static RAM |
|---|---:|---:|
| CH32V003 debug, AT24C256 | 13,070 / 16,384 bytes | 112 / 2,048 bytes |
| CH32V003 release, AT24C256 | 10,750 / 16,384 bytes | 112 / 2,048 bytes |

Build the image used for GDB with:

```sh
tools/build.sh ch32v003-debug
```

Because `-Og` may optimise temporary local variables, GDB can show locals such
as `read_a`, `read_b`, `valid_a`, and `valid_b` as `<optimized out>`. This does
not mean the code failed. Test function return values and persistent module
globals instead.

On RV32, scalar function return values are placed in register `a0`. After
`finish`, use this when GDB does not print a source-level return value:

```gdb
print $a0
```

## What has already been proven on hardware

The following observations were made:

1. RS485 device discovery works.
2. The device responds as uncommissioned address `0xff`.
3. The reported UID is:

   ```text
   cdab1ff853bc3f60ffffffff
   ```

4. `eeprom_read()` returned `true` while testing boot storage access.
5. `ASSIGN_ADDRESS_BY_UID` reached `config_store_commit()` with the correct
   candidate values:

   ```text
   values->address = 0x23
   values->fields.startup_mask = 0x00
   values->output_delay_units = 5
   ```

6. `config_store_commit()` returned `false`.
7. A subsequent scan still reported address `0xff`.

Therefore:

- RS485 receive and UID matching work.
- The commissioning payload contains the correct UID and requested address.
- The failure is inside the EEPROM transaction or the record readback/validation
  that follows it.
- Successful discovery does not itself prove EEPROM operation because discovery
  reads the MCU UID and does not access external EEPROM.

The observed host commands were:

```sh
python3 tools/device_tool.py --port /dev/ttyACM1 scan --duration 10
```

which returned:

```text
address: 0xff uid: cdab1ff853bc3f60ffffffff
devices: 1
```

Address assignment was attempted with:

```sh
python3 tools/device_tool.py \
  --port /dev/ttyACM1 \
  set-address \
  --uid cdab1ff853bc3f60ffffffff \
  --new-address 0x23
```

and returned:

```text
error: response timed out
```

`set-address` broadcasts the assignment and expects no response to that
broadcast. It then waits briefly and sends `PING` to the requested new address.
The timeout means the verification PING did not receive a response at `0x23`.
In this case GDB confirmed that the underlying configuration commit returned
false, so the device correctly retained address `0xff`.

## Critical GDB operating rule

After a breakpoint or `finish`, the MCU is halted. Sending an RS485 command
while GDB displays its `(gdb)` prompt cannot trigger another breakpoint because
the firmware is not running.

The correct order is always:

1. Set or enable the breakpoint in GDB.
2. Enter `continue` in GDB.
3. Confirm GDB prints `Continuing.` and no `(gdb)` prompt is present.
4. Leave GDB waiting.
5. Run the host command from a second terminal.

Stopping at a breakpoint will usually make the host command time out. That is
expected because GDB paused the firmware before its RS485 response. The GDB
return value and state are the result being tested in that attempt.

## Next diagnostic: isolate `eeprom_write()`

The next required test is whether the physical write and its first readback
succeed.

In GDB, disable the old `config_store_commit` breakpoint and break at the
physical write boundary:

```gdb
info breakpoints
disable 1
break eeprom_write
continue
```

The breakpoint number may not be `1`; use the number shown by
`info breakpoints` for `config_store_commit`.

After GDB prints `Continuing.`, run this in a second terminal:

```sh
python3 tools/device_tool.py \
  --port /dev/ttyACM1 \
  --timeout 1.0 \
  set-address \
  --uid cdab1ff853bc3f60ffffffff \
  --new-address 0x23
```

When GDB stops in `eeprom_write()`, inspect the request:

```gdb
print/x address
print length
```

For the first commit, expect:

```text
address = 0x0
length = 64
```

Then run the complete physical transaction:

```gdb
finish
```

If GDB does not show the result, immediately use:

```gdb
print $a0
```

Interpretation:

| Result | Meaning |
|---:|---|
| `1` / `true` | Page programming, ACK polling, and the first byte-for-byte readback succeeded |
| `0` / `false` | The physical write, readiness polling, or first readback failed |

## If `eeprom_write()` returns false

### Check for a readback mismatch

The strongest first hypothesis is that the EEPROM acknowledges communication
but does not program its array. This occurs when the EEPROM write-protect input
is high. Reads and address ACKs can still work while writes are prevented.

Disable prior breakpoints and break at the readback failure:

```gdb
disable
break eeprom.c:548
continue
```

Line numbers can move as comments or code change. Verify the current line with:

```sh
nl -ba src/eeprom.c | sed -n '535,555p'
```

The intended source statement is the `return false` associated with:

```c
if (!read_byte((uint16_t)(address + i), &actual) || actual != data[i]) {
  return false;
}
```

After entering `continue`, retry `set-address` from the second terminal. If the
breakpoint triggers, inspect:

```gdb
print i
print/x address + i
print/x actual
print/x data[i]
print/x I2C1_STAR1
```

For slot A, the first four expected bytes are the record magic:

```text
address 0x0000: 0x4d  ('M')
address 0x0001: 0x46  ('F')
address 0x0002: 0x43  ('C')
address 0x0003: 0x31  ('1')
```

If `actual` remains `0xff` while `data[i]` is one of these programmed values,
check the AT24C256 `WP` pin first. `WP` must be LOW for writes. A high or floating
high write-protect input can allow successful reads while preventing array
programming.

Also verify:

- SDA and SCL have external pull-up resistors.
- EEPROM ground and supply are correct.
- The AT24C256 hardware address pins select I2C address `0x50`.
- The fitted part is actually an AT24C256-compatible device.

### Check for write-cycle ACK-poll timeout

If the readback mismatch breakpoint does not trigger, test the readiness-poll
deadline. Locate the current source line containing the timeout return:

```sh
nl -ba src/eeprom.c | sed -n '520,540p'
```

Set a breakpoint on the `return false` inside:

```c
while (!device_ready()) {
  if ((uint32_t)(hal_ticks() - poll_started) >=
      EEPROM_WRITE_TIMEOUT_TICKS) {
    return false;
  }
}
```

For the current source revision this was approximately:

```gdb
disable
break eeprom.c:535
continue
```

If it triggers, inspect:

```gdb
print/x I2C1_STAR1
print/x I2C1_STAR2
print/x I2C1_CTLR1
```

This means the EEPROM did not ACK its address within 10 ms after STOP started
the internal write cycle.

### Check for failure to address the EEPROM for writing

Locate the failure immediately following `start_and_address(false)`:

```sh
nl -ba src/eeprom.c | sed -n '485,500p'
```

For the current revision it was approximately:

```gdb
disable
break eeprom.c:491
continue
```

If it triggers, inspect:

```gdb
print/x I2C1_STAR1
print/x I2C1_STAR2
print/x I2C1_CTLR1
```

This indicates that START or the EEPROM write-address acknowledgement failed
before any internal memory address or configuration data was sent.

## If `eeprom_write()` returns true

If `eeprom_write()` returns true, physical page programming and its internal
readback succeeded. The remaining possible failures in `config_store_commit()`
are:

1. The second complete `eeprom_read()` failed.
2. `decode_record()` rejected magic, schema, used length, field ranges, or CRC.
3. The decoded sequence did not equal the requested next sequence.
4. The decoded configuration did not equal the requested configuration.

Break at `eeprom_read()`:

```gdb
disable
break eeprom_read
continue
```

Retry assignment. The important read after `eeprom_write()` should use:

```text
address = 0x0000
length = 64
```

Use:

```gdb
finish
print $a0
```

If that read returns true, inspect decoding:

```gdb
disable
break decode_record
continue
```

At `decode_record()`:

```gdb
print/x record[0]@18
```

The beginning of a valid first record should resemble:

```text
0x4d, 0x46, 0x43, 0x31, 0x01, 0x12, ...
```

The first four bytes are `MFC1`, byte four is schema version `1`, and byte five
is used length `18` (`0x12`).

## I2C registers used during diagnosis

### `I2C1_CTLR1` — I2C Control Register 1

This register commands the peripheral state machine.

| Field | Meaning in this driver |
|---|---|
| `PE` | Enable I2C1 |
| `START` | Generate START or repeated START |
| `STOP` | Generate STOP and release bus-controller ownership |
| `ACK` | Drive ACK after received data; cleared to NACK the final one-byte read |
| `SWRST` | Reset the I2C peripheral state machine |

### `I2C1_STAR1` — I2C Status Register 1

This register reports transfer events and errors.

| Field | Meaning |
|---|---|
| `SB` | START condition completed |
| `ADDR` | Slave address was acknowledged |
| `BTF` | Data register and shift register both completed transfer |
| `RXNE` | Receive data register contains a complete byte |
| `TXE` | Transmit data register can accept another byte |
| `BERR` | Illegal bus START/STOP condition detected |
| `ARLO` | Controller lost arbitration |
| `AF` | Acknowledge failure/NACK; expected temporarily while EEPROM is busy |
| `OVR` | Data register was not serviced in time |

### `I2C1_STAR2` — I2C Status Register 2

This contains companion controller/bus state. Reading `STAR1` followed by
`STAR2` is the hardware-defined sequence that clears `ADDR` and allows the
transfer state machine to continue.

### `I2C1_DATAR` — I2C Data Register

Writing queues the next address or data byte for transmission. Reading obtains
the completed received byte and clears the receive-data-ready condition.

## Successful end-to-end result

After the underlying problem is fixed, assignment should succeed:

```sh
python3 tools/device_tool.py \
  --port /dev/ttyACM1 \
  --timeout 1.0 \
  set-address \
  --uid cdab1ff853bc3f60ffffffff \
  --new-address 0x23
```

Then scan:

```sh
python3 tools/device_tool.py \
  --port /dev/ttyACM1 \
  --timeout 1.0 \
  scan --duration 3
```

Expected:

```text
address: 0x23 uid: cdab1ff853bc3f60ffffffff
devices: 1
```

Inspect persistent state in GDB after a successful commit:

```gdb
print active_record_valid
print active_slot
print active_sequence
print/x active_values.address
```

Expected after the first successful commit:

```text
active_record_valid = true
active_slot = 0
active_sequence = 1
active_values.address = 0x23
```

Finally reset the MCU and break at `config_store_init()`. Its return value should
now be `CONFIG_STORE_LOADED` (`0`) rather than `CONFIG_STORE_DEFAULTED` (`1`).
That proves the configuration survived reset and passed record CRC validation.

## Factory-reset test after assignment works

Once the device is commissioned at `0x23`, restore factory defaults with:

```sh
python3 tools/device_tool.py \
  --port /dev/ttyACM1 \
  --timeout 1.0 \
  factory-reset \
  --address 0x23
```

Afterward, scan should again report address `0xff`. On the next boot,
`config_store_init()` should return `CONFIG_STORE_LOADED` (`0`), not defaulted,
because factory reset writes and verifies an explicit defaults record.

Expected stored defaults are:

```text
address = 0xff
startup_mask = 0x00
timeout_ms = 0
timeout_mask = 0x00
brightness = 30
output_delay_units = 5
active_record_valid = true
```

