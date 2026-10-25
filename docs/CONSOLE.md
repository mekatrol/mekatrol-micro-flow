# Firmware Console

The Raspberry Pi Pico and Waveshare RP2040-Zero FreeRTOS firmware provides a
line-oriented management console over USB CDC. TinyUSB and console processing
run exclusively in the higher-priority USB task.

The current firmware does not enable UART uploads, flow execution, or
application GPIO. Commands for those portable console capabilities remain
parseable but report that the capability is disabled.

## Connect

Flash an RP2040 firmware image, boot normally, and connect the board's USB port.
The console usually appears as `/dev/ttyACM0` on Linux, a `COM` port on Windows,
or `/dev/cu.usbmodem*` on macOS. USB CDC does not use the terminal baud rate;
`115200` is a conventional terminal setting.

Configure the terminal to append LF or CRLF. The firmware does not execute a
command until it receives carriage return or line feed.

Linux examples:

```sh
picocom -b 115200 /dev/ttyACM0
screen /dev/ttyACM0 115200
```

In VS Code Serial Monitor, select Text mode and an LF or CRLF line ending.

## Commands

Commands are lowercase and case-sensitive.

| Command | Current response |
|---|---|
| `help` | Lists accepted command forms. |
| `info` | Identifies the FreeRTOS CDC firmware and reports the system clock. |
| `status` | Reports `freertos`, USB enabled, and flow disabled. |
| `tasks` | Reports both task stack high-water marks in `StackType_t` words. |
| `queues` | Reports console and USB queue high-water/drop counters. |
| `flow show` | Reports `ERR flow-disabled`. |
| `flow validate` | Reports `ERR invalid-flow`. |
| `flow start` | Reports `ERR unavailable`. |
| `flow stop` | Reports `ERR unavailable`. |
| `flow erase-staging` | Reports `ERR flow-disabled`. |
| `gpio list` | Returns an empty successful list. |
| `gpio read <id>` | Reports `ERR unsupported-resource`. |
| `gpio write <id> <0\|1>` | Reports `ERR unsupported-resource`. |
| `reboot` | Acknowledges the command and resets the RP2040. |

Example:

```text
> info
OK Micro Flow Raspberry Pi Pico FreeRTOS CDC v1
System clock: 125000000 Hz
> status
OK usb=enabled flow=disabled
> tasks
OK tasks=2 usb-stack-free=... status-stack-free=...
```

The prompt characters are illustrative and are not printed by the firmware.

Synchronous replies start with `OK` or `ERR`; asynchronous diagnostics start
with `LOG`. Empty lines are ignored, both common backspace encodings are
accepted, and lines longer than the console buffer are rejected.

## Troubleshooting

If a command produces no response:

1. Select LF or CRLF line endings.
2. Confirm that the selected port belongs to the board.
3. Close other programs using the same serial port.
4. Send `help` in lowercase with no surrounding spaces.
5. Reconnect USB after reflashing or rebooting and select the new port.
6. Confirm that an RP2040 build was flashed. The Pico 2 targets are currently
   compile/link smoke builds and do not provide this console.
