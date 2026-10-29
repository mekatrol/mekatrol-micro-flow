# Set up a new Output Expander

Commissioning gives a new Output Expander its permanent network address and
checks its outputs. You do not need to understand the firmware or protocol.

## What you need

- One or more powered Output Expanders.
- A USB-to-RS-485 adapter connected to RS-485 A, B, and signal ground.
- A computer with Python 3, `pyserial`, and this project.
- An unused address from 1 to 254.

The scan command is safe to use with several new or already commissioned units
connected. Each device waits for a UID-seeded random interval and defers when
another device replies, so their identity responses normally occur one at a
time. Every unit must still receive a different permanent address.

Examples use Linux port `/dev/ttyUSB0` and address 35 (`0x23`). On Windows the
port looks like `COM3`. Substitute your port and chosen address throughout.

## 1. Prepare the computer

Open a terminal in the `output-expander` folder. Install the serial package if
needed, then check the tool starts:

```sh
python3 -m pip install pyserial
python3 tools/device_tool.py --help
```

If the port is missing or access is denied, check the USB connection, use the
correct port name, and close any serial-terminal program using that port.

## 2. Check wiring and power

Turn power off before changing wires. Connect A to A, B to B, and ground to
ground, then restore power. A new unit shows the unconfigured LED pattern.

If discovery finds no devices, swap A and B because adapter labels are not
always consistent. Also confirm power and the common ground.

## 3. Scan the connected bus

Every unit has a unique factory identity. Broadcast a scan to find each UID and
its current address without needing to know any addresses beforehand:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 scan --duration 2
```

Example:

```text
address: 0x23 uid: 102030405060708090a0b0c0
address: 0xff uid: cdab1ff853bc3f60ffffffff
devices: 2
```

Results are printed as soon as each device answers; the tool does not wait for
the complete duration before showing them. While its answer is pending, a
device flashes blue. It stops flashing and returns to its normal green pattern
after answering. At the end of the listening window the tool broadcasts a
cancel message, which stops any device that is still waiting, and then prints
the total count.

Address `0xff` means that the device has not been commissioned. Addresses
`0x01` through `0xfe` are configured addresses. The scan also exposes an
accidental duplicate address because both rows have the same address but
different UIDs.

Copy the complete UID for the unit you want to commission or readdress. The UID
identifies that physical unit and prevents the address command from changing
another device. If it is difficult to tell which row belongs to which unit,
scan one branch at a time or temporarily power the other units off.

## 4. Assign the permanent address

Use the copied UID and your chosen address:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 set-address \
  --uid cdab1ff853bc3f60ffffffff \
  --new-address 0x23
```

The address is verified and stored in EEPROM, so it survives loss of power.
`0x00` is reserved for broadcasts and `0xFF` for new devices. Label the unit
with its address and record its location.

## 5. Confirm the address

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0x23
```

The UID must match step 3. A successful command briefly turns the LED blue. If
there is no reply, power-cycle and retry the new address; settings are not made
active until their EEPROM copy has been verified.

## 6. Test outputs safely

Make sure connected loads can be operated safely. Keep people clear of moving
equipment. Read all eight states:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 outputs --address 0x23
```

Turn channel 1 on and off:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 output --address 0x23 1 on
python3 tools/device_tool.py --port /dev/ttyUSB0 output --address 0x23 1 off
```

Repeat for channels 2 through 8. Finish with every channel OFF:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 outputs --address 0x23 --set 0x00
```

Read the interval used to stagger OFF-to-ON transitions:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 output-delay --address 0x23
```

If the installation requires a different interval, set a value from 500 ms to
10000 ms in 100 ms increments. For example, configure a one-second interval:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 output-delay --address 0x23 --set 1000
```

Requesting several outputs together is safe: the board accepts the complete
target immediately, then energizes at most one new output per configured
interval. Turning outputs OFF is immediate and is never delayed.

## 7. Power-cycle check

Turn the unit off, wait a few seconds, restore power, and run `scan` again. The
same UID should now be reported at the new address. You can also run `info` at
that address to read its firmware version. This confirms commissioning is
complete and persistent settings were restored. Repeat the address-assignment
steps for every `0xff` unit, always choosing a different address. Never reuse
an address on the same RS-485 network.

## Status light

| Light | Meaning |
|---|---|
| Unconfigured pattern | No saved network address |
| Gentle green pulse | Normal operation |
| Brief blue | A valid command was received |
| Flashing blue | Waiting to answer an active bus scan |
| Amber pattern | Controller communications lost; fallback outputs active |
| Red pattern | Communication data or saved configuration fault |

## Troubleshooting

- **No reply:** Check power, port, A/B polarity, common ground, and whether
  another program has the adapter open.
- **A device is missing from a scan:** Run a longer scan, up to 15 seconds, and
  check its power and bus wiring. The tool cancels remaining replies when the
  selected duration ends.
- **Two rows show the same address:** Use each row's UID with `set-address` to
  assign one device an unused address; UID targeting works even when addresses
  conflict.
- **Address command fails:** Re-enter the complete 24-character UID and choose
  a unique number from 1 to 254.
- **Red pattern remains after a power-cycle:** Stop using affected outputs and
  ask a technician to inspect wiring and configuration storage.
- **Unexpected output:** Set mask `0x00`, isolate load power, and have the
  output wiring checked.

Technical details are in [protocol.md](protocol.md); all CLI options are in
[TOOLS.md](TOOLS.md).
