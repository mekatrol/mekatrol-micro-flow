# Firmware Console

The Raspberry Pi Pico and Waveshare RP2040-Zero firmware provides a
line-oriented management console over USB CDC. Use it to inspect firmware
state, control flow execution, access logical GPIO resources, and reboot the
board.

The console is separate from the UART0 flow-upload transport on GPIO0 and
GPIO1.

## Connect to the console

Flash a Pico or RP2040-Zero firmware image, boot the board normally, and connect
its USB port to the host. The console usually appears as `/dev/ttyACM0` on
Linux, as a `COM` port on Windows, or as `/dev/cu.usbmodem*` on macOS.

The console uses USB CDC, so the selected baud rate does not affect the USB
link. `115200` is a convenient conventional setting.

### VS Code Serial Monitor

1. Select the board's serial port, such as `/dev/ttyACM0`.
2. Select **Text** view mode.
3. Set **Line ending** to **LF** or **CRLF**.
4. Start monitoring, enter `help`, and send it.

Do not use **None** for the line ending. The firmware does not execute a
command until it receives carriage return (`CR`) or line feed (`LF`).

### Linux terminal

Find the port:

```sh
ls /dev/ttyACM*
```

Open it with `picocom`:

```sh
picocom -b 115200 /dev/ttyACM0
```

Alternatively, use `screen`:

```sh
screen /dev/ttyACM0 115200
```

### Windows and macOS

On Windows, select the board's `COM` port in a serial terminal such as the VS
Code Serial Monitor, PuTTY, or Tera Term. On macOS, locate the device and open
it with a serial terminal:

```sh
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodemDEVICE 115200
```

Configure the terminal to append LF or CRLF when sending a line.

## Command reference

Commands are lowercase and case-sensitive. Arguments are separated by one
space.

| Command | Purpose |
|---|---|
| `help` | List the accepted command forms. |
| `info` | Identify the firmware and console protocol. |
| `status` | Show flow state, active image size, and fault count. |
| `tasks` | Show supervisor service counts and timing statistics. |
| `queues` | Show UART and USB queue high-water and dropped-byte counters. |
| `flow show` | Show active and staged flow image sizes. |
| `flow validate` | Validate the active flow image. |
| `flow start` | Validate and start the active flow. |
| `flow stop` | Stop flow execution and return GPIO resources to safe inputs. |
| `flow erase-staging` | Erase the staged upload without changing the active image. |
| `gpio list` | List the available logical GPIO resource IDs. |
| `gpio read <id>` | Read a logical GPIO resource. |
| `gpio write <id> <0\|1>` | Configure a resource as an output and drive it low or high. |
| `reboot` | Acknowledge the command and immediately reset the board. |

For example:

```text
> info
OK Micro Flow Raspberry Pi Pico CDC v1
> status
OK flow=stopped active-bytes=0 faults=0
> gpio write 0 1
OK
> gpio read 0
OK 1
> gpio write 0 0
OK
```

The prompt characters above are illustrative and are not printed by the
firmware.

## GPIO resources

`gpio list` currently reports logical resources `0`, `1`, `2`, and `3`, which
map to physical RP2040 GPIO2 through GPIO5 respectively. Use the logical number
in `gpio read` and `gpio write` commands.

The `status-led` text returned by `gpio list` describes the separate board
status LED; it is not accepted as a numeric GPIO command argument. A console
write changes the selected logical resource to output mode. Stopping a flow or
entering a failure/reset path returns the resources to inputs with low output
latches.

Check the circuit before driving a resource. Do not drive a pin connected to
another output, and ensure external voltages remain within the RP2040's limits.

## Responses

Synchronous replies use these prefixes:

- `OK` indicates success and may be followed by returned values.
- `ERR` indicates that the command could not be completed.
- `LOG` marks asynchronous firmware diagnostics and may appear between command
  replies.

Common errors are:

| Response | Meaning |
|---|---|
| `ERR unknown-command` | The command, capitalization, spacing, or arguments are invalid. |
| `ERR line-too-long` | The input line exceeded the 79-character command limit. |
| `ERR invalid-flow` | The active flow image failed validation. |
| `ERR unavailable` | The requested flow state transition is not currently possible. |
| `ERR unsupported-resource` | The GPIO resource ID is outside the supported range. |

Both common backspace encodings are supported while editing a line. Empty
lines are ignored.

## Troubleshooting

If a command is sent but no response appears:

1. Set the terminal line ending to LF or CRLF. This is the most common cause.
2. Confirm that the selected port belongs to the board and reconnect after
   reflashing or rebooting if its device name changed.
3. Close other programs using the same serial port; normally only one program
   can own it at a time.
4. Send `help` in lowercase with no leading or trailing spaces.
5. Disconnect and reconnect USB, then select the newly enumerated port.
6. Confirm that a Pico or RP2040-Zero build containing the USB CDC console was
   flashed. The current Pico 2 targets are compile/link smoke builds and do not
   provide this console.

If characters are visible in the terminal but `help` produces no reply, verify
the line-ending setting first. Local echo or a monitor's “sent message” log
only proves that the terminal accepted the text; it does not prove that a line
terminator was transmitted.
