# FreeRTOS Multicore Runtime Plan

## Current implementation

The RP2040 firmware now uses FreeRTOS Kernel V11.3.0 and its RP2040 SMP port.
The former cooperative scheduler and supervisor have been removed. Portable
code under `src/lib` remains independent of FreeRTOS.

The current firmware creates two statically allocated tasks:

| Task | Priority | Initial affinity | Responsibility |
|---|---:|---|---|
| `usb` | idle + 2 | core 0 | TinyUSB polling, USB CDC transport, and console parsing |
| `status` | idle + 1 | core 0 | Board status LED update every 500 ms |

FreeRTOS SMP is enabled for both RP2040 cores, but both application tasks are
initially pinned to core 0. This keeps ownership deterministic while the flow
execution domain is designed. Dynamic allocation is disabled; task control
blocks, stacks, and transport buffers have static lifetime.

The current milestone does not enable flow execution, uploads, application
GPIO, networking, external flash, or watchdog supervision.

## Target architecture

The intended partition remains:

```text
Core 0: system and communications
  USB, console, uploads, storage, diagnostics, networking

Core 1: deterministic control
  flow execution, safety, control cycle, time-critical I/O

PIO and DMA: bounded peripheral transfer engines
  configured by one owning task and reporting completion to that owner
```

Tasks should remain pinned by subsystem until measurements justify migration.
Each peripheral and mutable object must have one owner. Cross-core interaction
should use bounded queues, task notifications, or immutable snapshots rather
than shared writable state.

## Design constraints

- Keep all FreeRTOS, Pico SDK, core-affinity, ISR, PIO, and DMA types outside
  `src/lib`.
- Prefer statically allocated tasks and synchronization objects.
- Give every queue a fixed capacity, full policy, high-water counter, and
  rejected-message counter.
- Do not call USB, console, storage, formatting, or network code synchronously
  from the control task.
- Define safe output behavior before enabling application GPIO.
- Measure worst-case execution time, blocking time, stack margin, missed
  periods, and interrupt latency on hardware.
- Treat priority assignment as a design input, not proof of schedulability.

## Integration phases

### 1. Establish the kernel baseline

- Keep the current static USB and status tasks stable.
- Exercise USB enumeration, console commands, reboot, stack high-water marks,
  and status output for extended runs.
- Record firmware size and idle CPU behavior on Pico and RP2040-Zero.

### 2. Add the control domain

- Define a bounded flow-cycle API in portable code.
- Create a statically allocated control task and pin it to core 1.
- Give the task exclusive ownership of active flow state and safety state.
- Start with synthetic inputs and outputs before enabling physical resources.

### 3. Add bounded communication

- Publish immutable input/output, telemetry, and fault snapshots.
- Add bounded command and reply channels between the system and control
  domains.
- Specify overflow, timeout, cancellation, and stale-data behavior.
- Prevent control work from waiting indefinitely for core 0.

### 4. Restore capabilities deliberately

- Integrate upload framing and flow storage under a system-domain owner.
- Add logical GPIO only after ownership and safe-state behavior are defined.
- Add watchdog supervision with a health policy that covers both domains.
- Add PIO/DMA, networking, or flash only with explicit ownership and measured
  effects on control latency.

### 5. Produce hardware evidence

- Measure control-period jitter and worst-case response under simultaneous USB
  and management load.
- Force queue overflow, task stalls, stack pressure, and communication loss.
- Verify safe outputs and watchdog recovery.
- Repeat applicable tests on RP2350 when its FreeRTOS firmware integration is
  implemented.

Completion requires measurements and repeatable hardware-test records; a
successful compile/link is only an integration check.
