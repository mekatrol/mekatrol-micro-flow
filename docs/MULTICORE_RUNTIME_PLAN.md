# Multicore Real-Time Runtime Plan

## Status and purpose

This document is a proposed architecture and migration plan. It does not claim
that FreeRTOS, symmetric multiprocessing (SMP), the flow executor, networking,
external flash, PIO data paths, or the described multicore services are already
implemented. Each phase has explicit evidence requirements so that architectural
intent is not confused with measured real-time behaviour.

The plan covers the Raspberry Pi RP2040 and RP2350 targets. Its purpose is to
replace the current cooperative firmware dispatch model with a design in which:

- deadline-sensitive control work cannot be delayed indefinitely by console,
  USB, networking, validation, or flash operations;
- both CPU cores are used where parallel work provides a real benefit;
- each peripheral and mutable object has an unambiguous owner;
- PIO and DMA perform precise or repetitive transfers without continuous CPU
  attention;
- communication between timing domains is bounded, observable, and testable;
- the portable flow library remains independent of FreeRTOS and Raspberry Pi
  silicon; and
- the same architectural contracts can be implemented for RP2040 and RP2350,
  while their SoC-specific details remain separate.

The recommended high-level split is:

```text
Core 0: system and communications domain
  USB, console, network stack, MQTT, HTTP, upload, storage, diagnostics

Core 1: deterministic control domain
  flow execution, safety, control cycle, time-critical I/O, control PIO/DMA

PIO and DMA: autonomous transfer and waveform engines
  owned by one domain, configured by its owning core, reporting completion to it
```

This is intentionally a partitioned SMP design. FreeRTOS may run across both
cores, but tasks are initially pinned by subsystem. Unrestricted task migration
is not an initial objective.

## Why the current model needs to change

The existing Pico firmware registers services with `microflow_supervisor_t` and
runs them cooperatively. Numeric priority determines which ready callback starts
first, but a callback runs to completion. The configured service runtime budgets
are checked after execution; they detect an overrun but cannot stop it.

Consequently, a newly ready high-priority service must wait for the currently
running callback. Flow validation, console formatting, a USB service pass, or a
future network operation could therefore add directly to control latency. The
portable scheduler in `src/lib/runtime/scheduler.c` has the same fundamental
property: callback priority orders dispatch, but it does not preempt a callback.
The current Pico firmware also runs through the supervisor rather than that
scheduler, so changing only `scheduler.c` would not make the firmware
preemptive.

A preemptive kernel addresses CPU scheduling, but preemption alone is not a
complete real-time design. A high-priority task can still be delayed by disabled
interrupts, a lock held by a lower-priority task, a shared bus, flash activity,
or excessive interrupt traffic. The architecture must therefore combine fixed
ownership, bounded execution, asynchronous I/O, and measurement with the RTOS.

The classic Liu and Layland paper, [Scheduling Algorithms for Multiprogramming
in a Hard-Real-Time Environment](https://doi.org/10.1145/321738.321743), explains
the basis of fixed-priority scheduling and schedulability analysis. Its simple
utilisation bounds assume a single processor and idealised independent periodic
tasks, so they must not be copied mechanically onto this dual-core system. The
important lesson here is that deadlines require known execution costs, periods,
blocking times, and priority relationships. Merely labelling a task “high
priority” is not evidence that its deadline can be met.

## Architectural principles

### Split by timing domain, not by peripheral category

“I/O” is not one timing class. A human console UART and a UART carrying a
time-critical actuator protocol have different deadlines even though both use
the same peripheral type. Similarly, an I2C configuration EEPROM belongs in
the system domain, while an I2C sensor required by every control cycle belongs
in the control domain.

Every operation should first be classified as one of:

1. **Hard or safety-critical control:** missing the deadline can create an
   unsafe or invalid physical state.
2. **Firm control:** a late result has no value and should be discarded or
   treated as a fault.
3. **Soft real-time:** latency matters, but occasional delay is acceptable.
4. **Best effort:** throughput and eventual completion matter more than
   latency.

The first two classes normally belong to Core 1 or to autonomous PIO/DMA
hardware. Network protocols, human interaction, logging, and noncritical
storage normally belong to Core 0.

### Prefer ownership over locks

Each mutable object, driver instance, peripheral, PIO state machine, and DMA
channel must have one owner. Other code sends a request to that owner rather
than manipulating the resource directly.

This is stronger than “protect shared state with a mutex.” A mutex can make a
race-free design, but it also creates blocking and priority-inversion paths that
must be included in worst-case timing analysis. FreeRTOS mutexes provide a form
of priority inheritance, but FreeRTOS explicitly notes that priority inheritance
minimises rather than eliminates priority inversion; see the [FreeRTOS mutex
documentation](https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html).
Single-owner resources and message passing make both correctness and timing
easier to reason about.

### Keep the portable library portable

Code under `src/lib` must remain ISO C11 and must not include FreeRTOS, Pico SDK,
PIO, DMA, or core-affinity types. It should express portable contracts such as:

- validate and execute a flow;
- consume an input snapshot and produce an output snapshot;
- stage, accept, reject, start, and stop a flow;
- report bounded faults and diagnostics; and
- call injected platform capabilities.

FreeRTOS tasks, queue handles, ISR glue, task notifications, core affinity, and
hardware ownership belong in the RP2040/RP2350 SoC and Pico/Pico 2 board
integration. This retains host-testable deterministic flow semantics and avoids
making the product dependent on one RTOS.

### Static and bounded by default

Tasks, queues, buffers, and synchronization objects should use caller-supplied
static storage. FreeRTOS supports statically allocated tasks, queues, event
groups, semaphores, and mutexes, as described in its [static versus dynamic
allocation documentation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation).

Static allocation does not by itself prove bounded behaviour, but it makes RAM
use visible in the linker map, prevents runtime heap exhaustion, and supports
the repository's existing allocation-free direction. Every queue must have a
documented capacity, full policy, high-water counter, and rejected-message
counter.

## Recommended core responsibilities

### Core 0: system and communications domain

Core 0 owns work whose execution time is variable, bursty, externally paced,
or primarily concerned with management rather than physical control.

Its responsibilities are:

- TinyUSB device-stack servicing and USB CDC transport;
- the human console and command parser;
- network interface servicing and the TCP/IP stack;
- MQTT session management, subscriptions, publications, and reconnect logic;
- HTTP request parsing and response generation;
- flow and firmware upload framing;
- W25Q128JV external-flash reads, page programs, erases, and integrity checks;
- staging and validating new flow images;
- configuration persistence;
- formatting logs, metrics, and telemetry;
- publishing immutable control snapshots; and
- system management, update coordination, and noncritical indicators.

These operations are grouped because USB hosts, networks, clients, and flash
devices introduce delays that the firmware does not fully control. TCP may
retransmit, HTTP requests may be large, MQTT may reconnect, and NOR flash must
complete internal program or erase cycles. The [W25Q128JV datasheet](https://www.winbond-electronics.com/pdf-7d/w25q128jvsjq.pdf)
states that most commands are ignored while a program or erase cycle is in
progress. Storage is therefore an asynchronous service, not something the
control loop should wait for.

Core 0 should never write live control state or directly drive safety outputs.
It may request changes through the Core 0-to-Core 1 command channel.

### Core 1: deterministic control domain

Core 1 owns work whose correctness depends on a bounded response time or a
coherent view of physical inputs and outputs.

Its responsibilities are:

- the periodic or event-driven control cycle;
- the active flow image and all mutable flow execution state;
- safety state, interlocks, and transition to safe outputs;
- capture of coherent input snapshots;
- evaluation of one bounded flow cycle;
- construction and application of coherent output snapshots;
- time-critical GPIO, PWM, UART, SPI, or I2C operations;
- control-related PIO state machines and their DMA channels;
- stale-input, missed-cycle, and overrun detection; and
- publication of immutable telemetry and fault snapshots to Core 0.

Core 1 must be able to continue safe control when Core 0 is busy, disconnected,
or restarting a communication session. It must not synchronously call MQTT,
HTTP, console, USB, filesystem, or flash code.

### PIO and DMA: hardware workers, not another software core

PIO state machines and DMA channels should be treated as autonomous engines
under one core's ownership. RP2040 provides two PIO blocks with eight state
machines in total; its product specifications and datasheet describe PIO as a
mechanism for custom peripheral support. The [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
also describes DMA as performing bulk transfers on a processor's behalf, with
peripheral-to-memory, memory-to-peripheral, and memory-to-memory modes.
RP2350 has related but not identical hardware, so channel counts, IRQ routing,
and security details must come from the [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
rather than from RP2040 assumptions.

PIO should handle bit-level timing and protocol mechanics. DMA should move
samples or output words between FIFOs and fixed RAM buffers. CPU tasks should
handle completed buffers and semantic decisions. This separation reduces
interrupt frequency and makes CPU execution less sensitive to individual wire
events.

## Peripheral allocation policy

The initial allocation should be as follows. A board variant may override it
only after documenting the deadline and ownership consequences.

| Resource or function | Default owner | Rationale |
|---|---|---|
| USB device and TinyUSB state | Core 0 | Bursty communications work with no control-cycle role |
| Console UART/USB CDC | Core 0 | Human latency is soft real-time |
| Network interface and TCP/IP | Core 0 | Protocol processing has variable execution and buffering |
| MQTT and HTTP | Core 0 | Application communication must not block physical control |
| W25Q128JV data flash | Core 0 | Program and erase latency is variable and relatively long |
| Flow upload staging buffer | Core 0 | Upload is a management operation |
| Flow validation | Core 0 | Potentially substantial CPU work can be completed before activation |
| Active flow and executor state | Core 1 | One owner is required for deterministic execution |
| Safety and interlock GPIO | Core 1 | Response must not wait for communications |
| Ordinary status LED/UI GPIO | Core 0 | No strict deadline |
| Control-protocol UART/SPI | Core 1 | Data participates directly in control decisions |
| Configuration UART/SPI | Core 0 | Management traffic has no control-cycle deadline |
| Control sensor I2C | Core 1 | Acquisition age and completion must be bounded |
| Configuration I2C | Core 0 | Slow or failed devices must not delay flow execution |
| Control PIO state machine | Core 1 | It forms part of the deterministic data path |
| Communications PIO state machine | Core 0 | It forms part of the system data path |
| DMA paired with a peripheral/PIO SM | Same as peripheral | Split ownership would create register and buffer races |

The final assignment is made per peripheral instance, not per peripheral
class. For example, UART0 may be a Core 0 console while UART1 is a Core 1
control protocol.

## FreeRTOS execution model

### Use SMP with affinity as a partitioned design

Use one FreeRTOS SMP kernel with `configNUM_CORES` set for both cores and core
affinity enabled. Pin each initial application task to its domain. This obtains
one kernel, common queues and notifications, and consistent diagnostics without
allowing tasks to migrate unpredictably.

FreeRTOS's [SMP introduction](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/13-Symmetric-multiprocessing-introduction)
explains an important difference from single-core scheduling: a lower-priority
task can run on one core while a higher-priority task runs on another. Priority
therefore does not provide mutual exclusion across cores. The architecture must
use ownership and synchronization rather than assuming that lower-priority code
cannot execute concurrently.

Initially use core affinity for all hardware-owning and state-owning tasks.
Leave migration disabled by policy even if the kernel supports it. A later
measurement-driven change may allow a stateless compute task to run on either
core, but only after proving that its data and library dependencies are
multicore-safe.

### Proposed Core 1 task set

Priorities below are relative rather than final numeric values.

#### Safety task: highest application priority

The safety task responds to deferred safety events, confirms or extends the
minimal immediate action taken by hardware/ISR code, stops the active flow,
places outputs in their defined safe state, records a latched fault, and notifies
Core 0.

It must never wait for Core 0, flash, networking, or an unbounded mutex. Where a
hazard requires response faster than RTOS scheduling can guarantee, hardware,
PIO, a peripheral safety function, or a deliberately minimal ISR must first
force the safe condition. The task then performs the larger state transition.

#### Control-cycle task: high priority

This task is woken by a hardware timer, PIO/DMA buffer completion, or another
explicit release event. For each release it:

1. records the actual release timestamp;
2. verifies that the prior cycle completed;
3. adopts the latest complete input snapshot;
4. applies eligible commands at a defined cycle boundary;
5. executes exactly one bounded flow cycle;
6. constructs a complete output snapshot;
7. applies or arms outputs;
8. publishes metrics and a telemetry snapshot; and
9. blocks until the next release.

The cycle task must not poll continuously. Blocking lets other work execute and
makes unexpected CPU consumption visible. Its deadline, period, worst observed
execution time, release jitter, completion jitter, and missed-release count must
be measured.

#### Control-I/O task: medium-high priority

This task performs transactions that belong to the control domain but should
not run inside the flow evaluator, such as I2C sensor reads or a framed actuator
protocol. It should initiate asynchronous IRQ/DMA transfers, block while the
peripheral works, validate completion, and publish a new input snapshot.

Every transaction needs a timeout and a defined failure policy. I2C is
especially important: a target may clock-stretch, fail to acknowledge, or hold
a line low. A stuck bus must become an observable stale-input or hardware fault,
not an indefinitely blocked control cycle.

### Proposed Core 0 task set

#### USB/network service task

This task meets the servicing requirements of TinyUSB and the chosen network
stack. Exact priority depends on those integrations, but it must not acquire
control-domain locks. Received application requests are converted into bounded
messages for other Core 0 services or Core 1.

#### Protocol task

This task owns MQTT/HTTP application state. It validates authorization and
syntax, translates accepted requests into control commands, and serves replies
from immutable published snapshots. It never walks live Core 1 flow state.

#### Storage task

This is the sole W25Q128JV owner. It serializes reads, page programs, erases,
verification, and metadata updates. Requests are asynchronous and identify
caller-owned or pool-owned buffers whose lifetime is explicit. Timeouts and
status-register failures become completion errors.

#### Upload and validation task

This task receives a complete image into the inactive staging buffer, validates
format, length, checksum, capabilities, and resource requirements, and only then
offers it to Core 1. Expensive validation is deliberately performed outside the
control domain.

#### Console and diagnostics task

This low-priority task formats human-readable output and exposes metrics. Large
responses must be chunked or generated incrementally. Formatting must never
hold a lock needed by a higher-priority service.

### Interrupt policy

Each peripheral IRQ must be routed to the core that owns the peripheral. An ISR
may:

- acknowledge the interrupt;
- read or write the minimum hardware state needed to prevent data loss;
- swap a prepared DMA buffer;
- capture a timestamp;
- increment a bounded diagnostic counter; and
- notify the owning task with a `FromISR` API.

It must not parse protocols, execute flow blocks, format output, erase flash, or
wait for a lock. FreeRTOS documents queues as its primary intertask mechanism
and provides distinct ISR-safe operations; see [FreeRTOS queues](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues).
For a simple one-consumer wakeup, use a direct task notification rather than a
queue carrying no data. For structured data, use a bounded queue or publish a
completed buffer and notify its consumer.

Interrupt priorities and FreeRTOS syscall eligibility must be configured and
documented per architecture. RP2040 Cortex-M0+, RP2350 Cortex-M33, and RP2350
Hazard3 must not be assumed to share identical interrupt-controller details.

## Cross-core communication contracts

### Command channel: Core 0 to Core 1

Use a statically allocated, bounded queue of small command values. Do not send
unbounded strings or bulk flow images through it.

Candidate commands include:

```c
typedef enum {
    MICROFLOW_CONTROL_START,
    MICROFLOW_CONTROL_STOP,
    MICROFLOW_CONTROL_ACTIVATE_IMAGE,
    MICROFLOW_CONTROL_SET_PARAMETER,
    MICROFLOW_CONTROL_CLEAR_FAULT,
    MICROFLOW_CONTROL_FORCE_SAFE
} microflow_control_command_kind_t;

typedef struct {
    microflow_control_command_kind_t kind;
    uint32_t sequence;
    union {
        struct {
            uint8_t buffer_index;
            uint32_t length;
            uint32_t generation;
        } activate;
        struct {
            uint16_t parameter_id;
            int32_t value;
        } parameter;
    } data;
} microflow_control_command_t;
```

This is illustrative, not yet a public API. Actual fields and widths require a
requirements review and host tests.

Each command needs a sequence number so Core 0 can correlate acceptance,
rejection, or completion. The queue-full policy should reject new ordinary
commands and report backpressure. A safety action must not depend on space in a
general command queue; use a separate notification or dedicated reserved path.

### Event channel: Core 1 to Core 0

Use a second bounded channel for state transitions, command acknowledgements,
faults, and low-rate telemetry descriptors. Fault delivery must have reserved
capacity or its own queue so a telemetry burst cannot hide a fault.

High-rate telemetry should not copy complete sample arrays into an RTOS queue.
It should publish ownership of a fixed buffer or a compact snapshot generation.
Core 0 may drop superseded telemetry, but it must count drops. Control must not
block waiting for telemetry consumption.

### Snapshot publication

Core 1 publishes complete immutable snapshots rather than allowing Core 0 to
read live executor fields:

```c
typedef struct {
    uint32_t generation;
    uint32_t timestamp_us;
    uint32_t flow_state;
    uint32_t fault_flags;
    uint32_t cycle_count;
    uint32_t missed_cycle_count;
    /* Fixed, versioned public values follow. */
} microflow_control_snapshot_t;
```

Use double buffering or a small fixed pool. Core 1 fills an unpublished buffer,
performs the required memory publication operation, and publishes its index and
generation. Core 0 reads only published immutable data. Buffer ownership and
the memory-ordering mechanism must be explicit for both supported
architectures; `volatile` alone is not a general inter-core synchronization
primitive.

### Flow-image handover

Use at least two statically allocated image buffers:

```text
active buffer:  immutable and owned by Core 1
staging buffer: writable and owned by Core 0
```

Activation proceeds as a transaction:

1. Core 0 receives or reads an image into its staging buffer.
2. Core 0 validates length, checksum, format, block graph, and declared
   capabilities.
3. Core 0 sends an activation command containing buffer index, length, and
   generation.
4. Core 1 waits for a defined safe cycle boundary.
5. Core 1 validates the handover metadata and stops or quiesces the old flow.
6. Core 1 atomically adopts the staging buffer as its immutable active image.
7. Core 1 forces outputs to their specified transition state before starting
   the new flow.
8. Core 1 acknowledges the adopted generation.
9. The former active buffer is returned to Core 0 for future staging.

Core 0 must not mutate a buffer until ownership has been returned. Core 1 must
not execute directly from a buffer being programmed in external flash.

## PIO and DMA data-path design

### Input path

The preferred high-rate input path is:

```text
pins -> PIO RX FIFO -> DMA -> fixed sample buffer -> completion IRQ
                                                     |
                                                     v
                                              Core 1 notification
```

Use ping-pong buffers when acquisition is continuous:

1. DMA fills buffer A while buffer B is available to Core 1.
2. Completion hardware or the ISR immediately arms buffer B.
3. Core 1 validates and consumes buffer A.
4. Core 1 returns buffer A before buffer B completes.

If processing does not finish in time, apply a defined overrun policy. Do not
silently overwrite unread control samples. Depending on the signal, the policy
may drop the newest buffer, mark input stale, latch a fault, or enter the safe
state.

### Output path

The preferred precise output path is:

```text
Core 1 output snapshot -> fixed DMA buffer -> PIO TX FIFO -> pins
```

Core 1 prepares a complete output buffer and publishes it at a cycle boundary.
PIO produces the wire or pin timing. This is more deterministic than asking a
preemptible task to toggle pins at exact intervals.

### PIO/DMA ownership rules

- One domain owns each PIO state machine.
- The same domain owns its DMA channels and RAM buffers.
- Only the owner changes configuration or accesses FIFOs in normal operation.
- Completion IRQs are handled on the owner's core.
- PIO programs are loaded and state machines are configured during controlled
  initialization, not casually reconfigured during a control cycle.
- DMA buffer alignment, transfer size, pacing request, chaining, and overrun
  behaviour are documented beside the driver.
- DMA does not make a buffer race-free; ownership still changes explicitly.
- Bus contention is measured under simultaneous CPU, DMA, USB/network, and
  flash load.

RP2040 and RP2350 have bus fabrics that permit concurrent activity but still
have shared downstream resources. The RP2350 datasheet, for example, exposes
priority controls for core and DMA bus managers. Such controls may improve a
measured bottleneck, but they are a tuning step rather than a replacement for
bounded workloads and correct ownership.

## Storage design

The W25Q128JV is treated as a Core 0 data-storage peripheral. The storage task
owns its SPI or PIO-SPI implementation, chip-select GPIO, DMA channels, command
state, and buffers.

The storage API should be asynchronous:

```text
request -> queued by caller -> storage task starts operation
        -> device becomes ready -> validation -> completion event
```

Reads may use DMA for throughput. Page programming and erasure still include
the flash device's internal busy interval, so DMA does not make their completion
time deterministic. Polling of the status register should block or delay the
storage task, not spin at a priority that consumes a core.

The design must distinguish:

- the additional W25Q128JV used as application data storage; and
- the board's boot/XIP flash from which firmware may execute.

Programming the boot/XIP device can require special multicore coordination and
RAM-resident routines because normal instruction fetches may depend on it. That
procedure must use the relevant Pico SDK/SoC contract and must not be inferred
from the external data-flash driver. Firmware update is a separate safety and
recovery design concern.

Before a flow starts, all data required on every cycle should normally be in
SRAM. A control cycle must not synchronously fetch nodes or configuration from
external flash.

## Watchdog and fault containment

The hardware watchdog should be fed only after a health monitor has evidence
that all required domains progressed. An unconditional feed from an idle loop
can conceal a dead control task.

Maintain at least:

- a Core 1 control-cycle progress counter;
- a Core 1 safety-service progress or state indicator;
- a Core 0 system-service progress counter;
- per-driver completion timestamps where loss would be dangerous; and
- a latched reason for the most recent unhealthy decision.

The health monitor compares current values with prior values and defined time
limits. It feeds the hardware watchdog only when the required progress contract
is satisfied. The control domain should independently force outputs safe when a
control deadline is missed; waiting for a whole-device watchdog reset is often
too slow and loses diagnostic context.

Fault handling should define whether each fault is transient, latched until a
command, or reset-only. Queue overflow, stale input, PIO FIFO error, DMA error,
flash failure, invalid flow, cycle overrun, and communication loss must not all
collapse into one undifferentiated error flag.

## Timing and schedulability work

Before assigning final priorities, create a task timing table:

| Task/path | Core | Trigger/period | Deadline | WCET target | Maximum blocking | Failure response |
|---|---:|---|---|---|---|---|
| Immediate safety action | 1/hardware | safety event | TBD | TBD | none | force safe |
| Safety task | 1 | notification | TBD | TBD | none/unavoidable only | latch fault |
| Control cycle | 1 | periodic/event | TBD | TBD | bounded | safe state |
| Control I/O | 1 | periodic/event | TBD | TBD | bounded bus transaction | stale input/fault |
| USB/network service | 0 | event/timer | stack-specific | TBD | bounded Core 0 locks | reconnect/drop |
| Storage | 0 | request | operation-specific | n/a for control | device busy time | completion error |
| Protocol | 0 | message | soft | TBD | bounded Core 0 locks | reject/backpressure |

“WCET target” is initially a design limit and must later be supported by
measurement and, for hard requirements, analysis appropriate to the code and
hardware. Measure both normal and adversarial conditions: maximum-size flows,
full network traffic, USB transfer, simultaneous DMA, flash activity, queue
pressure, and cache/XIP-unfriendly code paths.

Priorities should follow deadline and blocking analysis rather than subsystem
importance alone. Avoid giving every task a distinct priority without reason.
Equal-priority time slicing can add jitter; disable it or avoid equal-priority
ready tasks in the control domain unless measurement justifies it.

## Memory plan

FreeRTOS's principal per-task RAM cost is its stack. Define each task with
static storage, an initial evidence-based stack budget, overflow checking, and
runtime high-water instrumentation. Test high-water marks under worst-case
protocol nesting, formatting, validation, and fault paths, then retain a
documented margin.

Account explicitly for:

- every task control block and stack;
- RTOS queue storage;
- two flow-image buffers;
- control input/output snapshots;
- PIO/DMA ping-pong buffers;
- USB and network packet buffers;
- MQTT/HTTP working buffers;
- W25Q128 page/verification buffers;
- logs and diagnostics; and
- library static/BSS usage.

RP2040 has 264 KiB SRAM, while RP2350 has a different memory system and larger
resources. Do not let the RP2350 budget conceal an RP2040 overflow. Produce a
map and a target-specific static RAM budget for each supported configuration.

## Repository structure

The intended dependency direction remains:

```text
board application and resource mapping
        |
        v
RP2040/RP2350 FreeRTOS, IRQ, PIO, DMA, peripheral adapters
        |
        v
portable src/lib contracts and flow engine
```

Suggested future modules are conceptual and should be adjusted to existing
repository conventions during implementation:

```text
src/lib/runtime/
  control_protocol.c       portable command/event validation where useful
  control_snapshot.c       portable snapshot representation where useful

src/boards/rpi/rp2040/
  rp2040_freertos.c        port integration and SoC-level RTOS hooks
  rp2040_pio_*.c           SoC drivers, not board pin mappings
  rp2040_dma_*.c

src/boards/rpi/rp2350/
  rp2350_freertos.c
  rp2350_pio_*.c
  rp2350_dma_*.c

src/boards/rpi/raspberry-pi-pico*/
  application tasks, physical pins, logical resources, task composition
```

FreeRTOS should be a pinned, documented target dependency and must not be
required for host regression tests or for consumers of the portable library.
Record its source, version, licence, configuration, enabled port, local changes,
and update procedure in dependency provenance.

## Migration phases

### Phase 0: write measurable requirements

1. Define the control period, deadline, acceptable jitter, and safe-state
   response time.
2. Classify every I/O channel by timing class and failure consequence.
3. Identify which UART, SPI, I2C, GPIO, PIO, DMA, and flash instances exist on
   each board.
4. Define maximum flow size, block count, execution cost policy, telemetry rate,
   and communication load.
5. Define behaviour for stale sensors, queue overflow, network loss, flash
   failure, and missed cycles.

Exit criterion: every claimed real-time path has a deadline and a failure
policy. Without this, priority selection is guesswork.

### Phase 1: introduce domain ownership without FreeRTOS

1. Split `microflow_firmware_t` into explicit control-domain and system-domain
   state rather than passing one aggregate object everywhere.
2. Make ownership visible in APIs and file placement.
3. Replace console inspection of live `flow_store` and supervisor fields with
   immutable diagnostic snapshots.
4. Define and host-test command, event, snapshot, and buffer-handover contracts.
5. Retain the cooperative runtime temporarily while tests establish equivalent
   behaviour.

Exit criterion: prohibited cross-domain state access can be found by review and
the application functions through the message contracts.

### Phase 2: integrate FreeRTOS statically on one core

1. Pin and document a FreeRTOS kernel version and RP2040/RP2350 port status.
2. Add target-only configuration with static allocation.
3. Create tasks and channels using static storage.
4. Run the existing workload on Core 0 first to validate task lifecycle,
   interrupts, TinyUSB integration, timers, watchdog, and diagnostics.
5. Keep `src/lib` and host builds independent of FreeRTOS.

Exit criterion: one-core firmware passes existing behaviour tests, reports
stack/queue high-water data, and has no unexplained memory growth or watchdog
resets.

### Phase 3: establish the Core 1 control domain

1. Start the SMP scheduler and pin the control-cycle task to Core 1.
2. Move ownership of the active flow and executor state to Core 1.
3. Implement command and event queues plus snapshot publication.
4. Move safety and time-critical GPIO ownership to Core 1.
5. Exercise Core 0 with artificial worst-case load while measuring Core 1
   release and completion jitter.

Exit criterion: Core 1 meets its provisional deadline under sustained Core 0
load, and deliberate Core 0 stalls do not prevent safe control behaviour.

### Phase 4: transactional flow activation

1. Add double-buffered flow images.
2. Perform upload and full validation on Core 0.
3. Activate only at a documented Core 1 cycle boundary.
4. Add generation and sequence acknowledgements.
5. Test rejection, timeout, interrupted upload, repeated activation, and
   rollback/safe-stop paths.

Exit criterion: stress tests cannot expose a partially updated image to the
executor and activation never creates an uncontrolled output transition.

### Phase 5: add PIO/DMA control paths

1. Select one high-value control peripheral.
2. Implement fixed-buffer PIO/DMA acquisition or output.
3. Route completion to Core 1 with minimal ISR work.
4. Add overrun, FIFO error, DMA error, and stale-data diagnostics.
5. Repeat per peripheral rather than building a generic unmeasured framework.

Exit criterion: hardware tests demonstrate bounded data movement, correct
overflow behaviour, and improved or unchanged control jitter.

### Phase 6: add external storage

1. Implement the single-owner Core 0 storage task.
2. Add asynchronous operations with timeouts and completion results.
3. Add power-loss-aware metadata and image integrity rules.
4. Confirm flash program/erase traffic does not disturb Core 1 deadlines.
5. Keep boot/XIP update handling separate from ordinary W25Q128 data storage.

Exit criterion: power interruption and busy-device tests preserve the last
known-valid flow and control timing remains within limits.

### Phase 7: add networking and application protocols

1. Integrate the network interface and stack on Core 0.
2. Add MQTT and HTTP as clients of the command/snapshot interfaces.
3. Bound request sizes, connection counts, buffers, and publication rates.
4. Apply authentication and authorization before producing control commands.
5. Stress reconnects, malformed traffic, slow clients, and full queues while
   measuring Core 1.

Exit criterion: hostile or maximum permitted communication load cannot mutate
live state directly, exhaust unbounded memory, or violate control deadlines.

### Phase 8: watchdog, fault injection, and conformance

1. Replace unconditional watchdog servicing with progress-based health checks.
2. Inject stopped tasks, stuck queues, stale sensors, PIO/DMA overruns, flash
   failures, network floods, and missed cycles.
3. Verify safe outputs and persistent fault evidence.
4. Run equivalent applicable tests on RP2040 and RP2350 Arm/RISC-V targets.
5. Record map files, RAM/flash budgets, stack margins, cycle distributions, and
   long-duration results.

Exit criterion: the target meets documented deadlines and fault responses with
reproducible evidence; unsupported target variants remain labelled partial or
experimental.

## Verification matrix

At minimum, automate or document the following tests:

| Test | Expected evidence |
|---|---|
| Maximum flow under idle communications | cycle WCET and jitter distribution |
| Maximum flow plus USB saturation | no missed control deadline |
| Maximum flow plus MQTT/HTTP load | no control-state race or deadline miss |
| External flash erase/program | Core 1 remains within deadline |
| Continuous PIO/DMA input | no unread-buffer overwrite; counters remain valid |
| Forced slow Core 0 task | Core 1 remains safe and independent |
| Forced slow Core 1 cycle | overrun recorded and safe policy executed |
| Command queue full | bounded rejection/backpressure; no corruption |
| Telemetry queue full | telemetry drops counted; control never blocks |
| Fault channel pressure | safety fault still delivered/latches locally |
| Flow activation during execution | swap occurs only at boundary |
| Power loss during external-flash update | last valid image recoverable |
| Stuck I2C target | timeout, recovery attempt, stale/fault policy |
| Core 0 heartbeat stopped | watchdog policy detects system-domain failure |
| Core 1 heartbeat stopped | outputs safe and watchdog reset occurs as designed |

Measure tails, not just averages. Store maximum observed runtime and preferably
a histogram or percentile distribution during development. A clean ten-second
run is not enough evidence for an automation controller.

## Decisions deliberately deferred

The following require product requirements or measurement before they can be
finalised:

- exact FreeRTOS version and configuration;
- numeric task and interrupt priorities;
- control period and deadline;
- whether networking uses onboard wireless, external Ethernet, or another
  interface;
- which UART/I2C/SPI instances are control-facing;
- whether the W25Q128JV uses hardware SPI or PIO SPI;
- exact queue capacities and overflow policies;
- flow image and snapshot sizes;
- whether RP2350 Arm and Hazard3 ports have equivalent required SMP support;
- cache/XIP placement and any RAM-resident critical code;
- watchdog ownership and timeout; and
- the safety integrity requirements of the final product.

These are not implementation details to guess. They determine schedulability,
memory use, driver ownership, and verification scope.

## References

Primary and foundational references used by this plan:

1. C. L. Liu and James W. Layland, [Scheduling Algorithms for Multiprogramming
   in a Hard-Real-Time Environment](https://doi.org/10.1145/321738.321743),
   *Journal of the ACM*, 20(1), 1973. Foundational fixed-priority scheduling and
   utilisation analysis; its assumptions must be checked before application.
2. FreeRTOS, [Task scheduling: single-core, AMP and
   SMP](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/04-Task-scheduling).
   Describes fixed-priority preemption and the different concurrency semantics
   of single-core, AMP, and SMP systems.
3. FreeRTOS, [Symmetric multiprocessing
   introduction](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/13-Symmetric-multiprocessing-introduction).
   Covers core affinity and why lower- and higher-priority tasks may execute
   simultaneously on different cores.
4. FreeRTOS, [Queues](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues),
   [binary semaphores](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/02-Binary-semaphores),
   and [mutexes](https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html).
   Defines task/ISR communication primitives and priority-inheritance limits.
5. FreeRTOS, [Static versus dynamic memory
   allocation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation).
   Documents caller-supplied RTOS-object storage.
6. Raspberry Pi Ltd., [RP2040
   datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf).
   Authoritative source for RP2040 cores, bus fabric, interrupts, PIO, DMA,
   peripherals, SRAM, and XIP behaviour.
7. Raspberry Pi Ltd., [RP2350
   datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
   Authoritative source for RP2350 Arm/RISC-V, bus, interrupt, PIO, DMA, memory,
   and security differences.
8. Raspberry Pi Ltd., [Pico SDK hardware
   APIs](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html).
   SDK-level APIs for multicore, synchronization, IRQ, PIO, DMA, flash, and
   peripherals; use the pinned SDK version selected by this project.
9. Winbond Electronics, [W25Q128JV
   datasheet](https://www.winbond-electronics.com/pdf-7d/w25q128jvsjq.pdf).
   Authoritative command, busy-state, timing, program, erase, and electrical
   requirements for the specified external NOR flash family. Confirm the exact
   suffix fitted to the board before fixing electrical and performance limits.

## Final architectural rule

The plan can be summarised in one enforceable rule:

> Core 1 owns deterministic physical control, Core 0 owns nondeterministic
> system work, PIO and DMA perform precise transfers for one explicit owner,
> and only bounded messages or immutable buffers cross the boundary.

FreeRTOS provides preemption and multicore scheduling, but this ownership rule
provides the comprehensible concurrency model. Both are required for a system
in which communication and storage can grow without quietly weakening control
timing.
