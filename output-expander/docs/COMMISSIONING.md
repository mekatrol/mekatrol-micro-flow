# Device commissioning

These steps discover one factory-default device, assign its temporary bus
address, verify the new address, and turn on an output.

Connect only one uncommissioned device while using address `0xFF`. Multiple
uncommissioned devices would respond simultaneously and cause an RS-485 bus
collision.

Run all commands from the `output-expander` directory:

```bash
cd /home/dad/repos/mekatrol-micro-flow/output-expander
```

## 1. Read the device information and UID

A factory-default device uses the uncommissioned discovery address `0xFF`.

```bash
python3 tools/device_tool.py --port /dev/ttyACM1 info --address 0xFF
```

Example response:

```text
address: 255
firmware: 1.0
uid: cdab1ff853bc3f60ffffffff
```

## 2. Assign the device address

Use the complete UID returned above. This example assigns address `0x23`:

```bash
python3 tools/device_tool.py --port /dev/ttyACM1 set-address \
  --uid cdab1ff853bc3f60ffffffff \
  --new-address 0x23
```

Valid commissioned addresses are `0x01` through `0xFE`. Do not assign `0x00`,
which is the broadcast address, or `0xFF`, which is reserved for uncommissioned
discovery.

## 3. Read information using the new address

```bash
python3 tools/device_tool.py --port /dev/ttyACM1 info --address 0x23
```

The returned UID should match the UID discovered at `0xFF`.

## 4. Turn on an output

Turn on output channel 1 using the new address:

```bash
python3 tools/device_tool.py --port /dev/ttyACM1 output \
  --address 0x23 1 on
```

To turn it off again:

```bash
python3 tools/device_tool.py --port /dev/ttyACM1 output \
  --address 0x23 1 off
```

## Current persistence limitation

Address assignment is currently stored in RAM only. Resetting or power-cycling
the device restores address `0xFF`; repeat the commissioning steps afterward.
