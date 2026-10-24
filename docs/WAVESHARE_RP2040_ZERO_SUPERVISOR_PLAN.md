# Waveshare RP2040 Zero Supervisor Bring-up Plan

## Purpose

This is an intermediate implementation plan between the current status-LED
firmware and Phase 3 of `PLANS.md`. It establishes the Waveshare RP2040 Zero as
the first useful standalone Micro Flow device without introducing an RTOS or
coupling the portable library to the RP2040.

The result will accept flow programs over a hardware UART, expose an
interactive debug and management console over native USB, and provide
board-owned logical GPIO and status-LED services. It will not yet claim full
RP2040 support or completion of Phase 3.

## Architectural boundary

The board runtime uses a small supervisor rather than a preemptive kernel. The
supervisor owns startup policy, service scheduling, bounded queues, flow-update
coordination, and fault handling. It lives under:

```text
src/boards/rpi/waveshare-rp2040-zero/
```

RP2040 interrupt handlers, register definitions, timer alarms, UART, USB device
controller support, and other reusable chip mechanisms remain under:

```text
src/boards/rpi/rp2040/
```

This keeps interrupt handlers in the SoC layer while ensuring supervisor policy
does not enter `src/lib`. Physical pin assignments remain in the Waveshare
board directory.

The portable flow engine must not include RP2040 headers or depend on the
supervisor. The supervisor may call the public Micro Flow API and adapt board
events to logical capabilities.

## Execution model

The initial system is interrupt-driven and run-to-completion:

1. Peripheral interrupt handlers do the minimum bounded work needed to service
   hardware.
2. Interrupt handlers copy bytes or compact events into statically allocated
   single-producer/single-consumer queues and set supervisor event flags.
3. The main-context supervisor drains ready work in a documented priority
   order.
4. Supervisor services and flow execution perform bounded work and return.
5. When no work is ready, the supervisor waits for the next interrupt.

Arbitrary flow functions do not run in interrupt context. Timing that cannot
tolerate supervisor latency belongs in a timer compare handler, PWM, PIO, or
DMA service with a narrow logical interface to the flow engine.

Every shared queue must document its producer, consumer, overflow behaviour,
and interrupt-masking requirements. No service may use dynamic allocation.

## Fixed hardware mapping

| Function | RP2040 resource | Waveshare pin |
|---|---|---|
| Flow upload receive | UART0 RX | GPIO1 |
| Flow upload transmit/status | UART0 TX | GPIO0 |
| Debug and management console | Native USB device, CDC ACM class | USB connector |
| Status LED | WS2812 data | GPIO16 |
| Initial general-purpose I/O | Board-defined logical resources | Unassigned until tested |

“USB UART” means a native USB CDC ACM serial interface. It is separate from
UART0 and does not consume GPIO0 or GPIO1.

## Stage 1: supervisor and interrupt foundation

Implementation status: complete in source. Host regression and firmware-build
verification are in place; the physical-board soak and repeated warm-reset run
remain hardware validation activities and are not inferred from compilation.

### Work

- Add the board-owned supervisor, service table, event flags, and fault state.
- Add RP2040 interrupt-vector dispatch needed by timer, UART0, and USB.
- Replace continuous scheduler polling with a timer alarm or interrupt-driven
  wake-up path.
- Define bounded byte-ring and event-queue types local to this board
  integration initially.
- Define overflow counters and deterministic policies: reject newest input for
  command streams, report the fault, and never overwrite an unread flow image.
- Add supervisor heartbeat, maximum service-runtime measurement, queue
  high-water marks, and watchdog service points.

### Exit criteria

- The board runs for an extended test without dynamic allocation.
- Timer wraparound and repeated warm resets are exercised.
- Idle execution waits for interrupts rather than continuously polling.
- Queue overflow is observable and produces no memory corruption or deadlock.

### Implementation evidence

- Board-owned `supervisor.c/.h` provides the fixed service table, event flags,
  fault state, SPSC byte/event queues, priority dispatch, heartbeat, runtime
  metrics, queue high-water marks, and watchdog service points.
- RP2040 startup dispatches Timer 0, USB controller, and UART0 vectors. Alarm 0
  supplies a wrap-safe deadline wake, and the idle path closes the event/sleep
  race before executing `WFI`.
- Queue full policy rejects the newest item, preserves unread data, increments
  an observable counter, and raises a deferred supervisor fault when the
  supervisor-aware producer API is used.
- `microflow-supervisor-test` exercises queue saturation, ordering, timing,
  wraparound, idle waiting, watchdog servicing, fault delivery, and repeated
  initialization without allocation.
- Verified commands: `cmake --build --preset host-debug && ctest --preset
  host-debug`, the equivalent `host-release` build/test, and `cmake --build
  --preset waveshare-rp2040-zero-debug`.
- Current compile measurement: 2,376 bytes text, 0 bytes initialized data, and
  292 bytes BSS (GNU Arm Embedded 13.2.1). This is compile evidence, not an
  on-device timing or soak result.

## Stage 2: UART0 flow-upload transport

Implementation status: complete in source. Host regression and firmware-build
verification cover the bounded protocol implementation. Physical UART upload,
overflow, disconnect, and sustained-board tests remain hardware validation
activities, so the Stage 2 exit criteria are not yet claimed complete.

### Work

- Implement project-owned RP2040 UART0 register support.
- Map UART0 TX to GPIO0 and RX to GPIO1 in the board layer.
- Use RX interrupts to place received bytes into a bounded ring buffer.
- Drain and parse input in supervisor context, not in the ISR.
- Begin with an explicitly versioned binary frame containing magic, protocol
  version, message type, payload length, sequence number, and checksum/CRC.
- Support at least: probe, device information, begin upload, data chunk,
  complete upload, abort upload, validation result, and activation result.
- Receive into a bounded staging area. Validate the complete candidate before
  replacing the active flow. A failed or interrupted upload leaves the active
  flow unchanged.
- Add TX buffering for acknowledgements, errors, and upload progress.

Initial uploads may remain in RAM. Persistent flash storage is a separate step
because safe erase/program operations, power-loss recovery, and wear policy
must be designed and tested explicitly.

### Exit criteria

- A host test utility can upload a valid in-memory flow over GPIO0/GPIO1.
- Corrupt, oversized, truncated, duplicate, and out-of-order frames are
  rejected deterministically.
- UART queue overflow and host disconnect recovery are verified.
- Flow validation completes before activation and malformed data never enters
  execution.

### Implementation evidence

- Project-owned RP2040 UART0 register support configures GPIO0/GPIO1 for
  115200 8N1 operation and uses the existing UART0 interrupt vector.
- Board-owned RX and TX SPSC rings keep interrupt work bounded; parsing,
  staging, validation, and activation run as a supervisor service.
- Version 1 `MFLO` framing, sequence rules, CRC-32, status codes, the bounded
  2048-byte staging slot, and the `MFFL` RAM image envelope are documented in
  `docs/WAVESHARE_RP2040_ZERO_UART_PROTOCOL.md`.
- Candidate data cannot replace the active RAM slot until ordering, total
  length, image CRC, and image-envelope validation all pass. Abort and receive
  timeout discard staging without modifying the active slot.
- `microflow-upload-transport-test` covers valid upload and activation, abort,
  corrupt CRC, duplicate/out-of-order sequence, invalid offset, incomplete
  upload, oversized framing, parser resynchronization, and receive timeout.
- `tools/microflow_uart_upload.py` probes and performs chunked, acknowledged
  uploads through any pyserial-supported host port.
- Current firmware compile measurement is 5,468 bytes text, 0 bytes initialized
  data, and 5,012 bytes BSS with GNU Arm Embedded 13.2.1. The two 2,048-byte
  RAM slots intentionally dominate BSS. This is compile evidence, not an
  on-device throughput or reliability result.
- The payload remains opaque after its validated envelope because the portable
  engine does not yet define serialized flow semantics. Activation is safe RAM
  image replacement, not execution; unvalidated bytes are never presented as
  an executable flow.

## Stage 3: native USB CDC debug console

Implementation status: in progress. The bounded board-owned console and its
host regression tests are implemented. The failed project-owned USB controller
has been removed and replaced with pinned Pico SDK 2.3.1 TinyUSB CDC support.
Firmware builds successfully; enumeration, reconnect, concurrent-load, and
cross-platform hardware validation remain outstanding.

### Work

- Use the pinned Raspberry Pi Pico SDK and its maintained TinyUSB device stack
  for one CDC ACM interface. Link only the components required by this board
  firmware and record their source, version, licence, configuration, local
  modifications, and update procedure.
- Keep Pico SDK and TinyUSB headers and types inside the RP2040 SoC and
  Waveshare board layers. The portable library and its public interfaces remain
  independent of both dependencies.
- Keep USB endpoint interrupt work bounded; transfer console bytes through
  static queues to supervisor context.
- Provide line editing with fixed buffers and explicit truncation behaviour.
- Start with these console commands:

```text
help
info
status
tasks
queues
flow show
flow validate
flow start
flow stop
flow erase-staging
gpio list
gpio read <logical-resource>
gpio write <logical-resource> <0|1>
reboot
```

- Gate mutating commands through the same staging, validation, and activation
  path used by UART uploads; do not create a second flow-update mechanism.
- Separate diagnostic output from command responses sufficiently that an
  asynchronous log line cannot be parsed as a command result.

### Exit criteria

- The board enumerates consistently after cold boot, warm reset, and repeated
  cable reconnects on Windows, macOS, and Linux.
- Console commands remain responsive while UART data is arriving and a flow is
  executing.
- Flooded debug output cannot block the supervisor or flow execution; excess
  diagnostics are dropped with a visible counter.
- Invalid and overlong commands cannot escape fixed buffers.

### Current implementation evidence

- Pico SDK 2.3.1 now supplies startup, clocks, RP2040 peripheral support, and
  TinyUSB CDC. The custom failed USB controller has been removed. A clean build
  produces ELF, map, BIN, and UF2 artifacts without downloading dependencies.
- The board-owned console provides the planned commands, fixed 80-byte line
  editing, backspace handling, and deterministic discard/recovery for overlong
  input. Unsupported GPIO resources are reported until Stage 4 assigns logical
  resources.
- Responses use `OK`/`ERR` records and asynchronous diagnostics use `LOG`
  records. Output never waits for the host; full transmit storage drops bytes
  and increments the visible `usb-tx-dropped` counter reported by `queues`.
- Flow start requires the currently active RAM image to pass the same image
  validator used by UART activation. `flow erase-staging` clears and aborts the
  UART transport's single staging slot rather than introducing another update
  path.
- `microflow-console-test` covers command dispatch, numeric GPIO arguments,
  invalid commands, reboot dispatch, and overlong-line recovery.
- Verified commands: `cmake --build --preset host-debug && ctest --preset
  host-debug` and `cmake --build --preset waveshare-rp2040-zero-debug`.
- Current firmware compile measurement: 12,752 bytes text, 0 bytes initialized
  data, and 5,896 bytes BSS with GNU Arm Embedded 13.2.1. This is compile
  evidence, not proof of enumeration or USB electrical behavior.

## Stage 4: board hardware interface

Implementation status: complete in source for the initial GPIO map. Host
regression and firmware-build verification cover resource validation and safe
state transitions. Electrical GPIO tests and the WS2812/UART/USB interference
measurement remain physical-hardware activities. Executing GPIO from a flow
also remains blocked on the portable serialized-flow and capability contracts;
the current `MFFL` payload is intentionally opaque.

### Work

- Define board-local logical resources independently of RP2040 pin numbers.
- Adapt the GPIO operations required by the portable capability interface:
  configure direction, read, write, and report unsupported resources.
- Retain the GPIO16 WS2812 as a logical status indicator.
- Move WS2812 generation from timing-sensitive software bit banging to PIO if
  measurements show that interrupt masking interferes with UART, USB, or flow
  deadlines.
- Reserve pins used by UART0, USB, flash, debug, and the status LED so a flow
  cannot claim them as ordinary GPIO.
- Validate every requested logical resource and mode before starting a flow.

### Exit criteria

- A validated flow can read and write assigned logical GPIO resources.
- A flow cannot access reserved or nonexistent pins.
- UART and USB traffic do not corrupt WS2812 output, and status updates do not
  cause unacceptable communication latency.
- GPIO capability rejection and reset-to-safe-state behaviour are tested.

### Current implementation evidence

- Board-local logical GPIO resources 0 through 3 map to GPIO2 through GPIO5.
  The physical mapping is private to the board layer. GPIO0/GPIO1 (UART0),
  GPIO16 (status WS2812), USB, flash, and debug resources are not in the map.
- GPIO direction, read, write, and safe-state operations use a board-owned
  adapter over the RP2040 SoC driver. Output configuration latches low before
  enabling drive. Writes to an input and access to nonexistent resources fail.
- Startup and `flow stop` return every general-purpose logical resource to an
  unpulled input with a low output latch. The status LED remains a separate
  board-owned logical indicator rather than an ordinary GPIO resource.
- The USB console lists and operates the same logical-resource adapter; it does
  not accept physical RP2040 pin numbers.
- `microflow-board-gpio-test` covers mapping, unsupported resources, direction
  enforcement, reads/writes, and reset-to-safe-state behavior.
- Verified commands: host debug and release build/test, plus
  `cmake --build --preset waveshare-rp2040-zero-debug`.
- Current debug compile measurement with Pico SDK/TinyUSB debug enabled:
  45,956 bytes flash text, 0 bytes initialized data, and 9,448 bytes BSS (GNU
  Arm Embedded 13.2.1). The firmware build now reports and enforces the board's
  2 MiB flash and 264 KiB SRAM limits. This is compile evidence, not electrical
  or timing validation.

## Stage 5: integrated demonstration and measurements

Implementation status: partially instrumented. Queue high-water/drop counters,
per-service dispatch counts and maximum observed runtime, supervisor heartbeat,
watchdog service count, and build-time flash/static-RAM limits are observable.
Stack high-water instrumentation and all latency, throughput, jitter, reset,
and soak measurements still require implementation or physical hardware.

### Demonstration

1. Connect a host uploader to UART0 on GPIO0/GPIO1 through a 3.3 V USB-to-UART
   adapter with a common ground.
2. Connect the board's native USB port for the management console.
3. Upload a flow that controls the status LED and at least one logical GPIO.
4. Inspect, validate, start, stop, and replace the flow from the USB console.
5. Inject malformed UART frames and saturated console logging while the flow
   continues operating.

### Measurements and evidence

- Record ELF, BIN, UF2, map, flash, static RAM, stack high-water marks, and
  queue high-water marks.
- Measure interrupt latency, supervisor dispatch latency, UART loss rate, USB
  throughput, flow-cycle time, and observed jitter.
- Test timer wraparound, buffer exhaustion, watchdog recovery, USB reconnect,
  UART framing errors, interrupted upload, cold reset, and warm reset.
- Record the exact toolchain and any imported USB artifact provenance.

### Exit criteria

- The demonstration operates for an extended soak test without memory growth,
  queue corruption, unintended flow replacement, or watchdog starvation.
- Timing and memory results are documented and compared with configured board
  limits.
- Known limitations are recorded before work proceeds to the broader RP2040
  Phase 3 plan.

## Deliberately deferred work

- FreeRTOS or another general-purpose preemptive kernel
- Running arbitrary flow callbacks in interrupt context
- RP2040 multicore scheduling
- Wireless or network transports
- Persistent flow storage until the RAM upload path is proven
- Authentication, encryption, and production firmware-update security
- A general cross-board console protocol until the RP2040 implementation and
  command model have been validated

