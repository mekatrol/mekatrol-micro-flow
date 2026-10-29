# Set up a new Output Expander

Commissioning gives a new Output Expander its permanent network address and
checks its outputs. You do not need to understand the firmware or protocol.

## What you need

- One powered Output Expander.
- A USB-to-RS-485 adapter connected to RS-485 A, B, and signal ground.
- A computer with Python 3, `pyserial`, and this project.
- An unused address from 1 to 254.

> **Important:** Connect only one brand-new, uncommissioned Output Expander
> during this procedure. Two new units would answer at the factory address at
> the same time. Existing commissioned units may remain connected.

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

If discovery times out, swap A and B because adapter labels are not always
consistent. Also confirm power, common ground, and that only one new unit is
connected.

## 3. Read the unit identity

Every unit has a unique factory identity. Read it at the discovery address:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0xFF
```

Example:

```text
address: 255
firmware: 1.0
uid: cdab1ff853bc3f60ffffffff
```

Copy the complete `uid`. It identifies this physical unit and prevents the
address command from changing another device.

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

## 7. Power-cycle check

Turn the unit off, wait a few seconds, restore power, and run `info` at the new
address again. The same UID confirms commissioning is complete and persistent
settings were restored. You can now repeat the guide for the next new unit.
Never reuse an address on the same RS-485 network.

## Status light

| Light | Meaning |
|---|---|
| Unconfigured pattern | No saved network address |
| Gentle green pulse | Normal operation |
| Brief blue | A valid command was received |
| Amber pattern | Controller communications lost; fallback outputs active |
| Red pattern | Communication data or saved configuration fault |

## Troubleshooting

- **No reply:** Check power, port, A/B polarity, common ground, and whether
  another program has the adapter open.
- **No reply at the factory address:** Disconnect other new units. A normal
  green pattern means this unit may already have an address.
- **Address command fails:** Re-enter the complete 24-character UID and choose
  a unique number from 1 to 254.
- **Red pattern remains after a power-cycle:** Stop using affected outputs and
  ask a technician to inspect wiring and configuration storage.
- **Unexpected output:** Set mask `0x00`, isolate load power, and have the
  output wiring checked.

Technical details are in [protocol.md](protocol.md); all CLI options are in
[TOOLS.md](TOOLS.md).
