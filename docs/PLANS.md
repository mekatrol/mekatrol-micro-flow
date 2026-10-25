# Mekatrol Micro Flow Development Plan

This roadmap is not evidence that a phase or target is complete. Record build,
hardware-test, timing, and memory evidence as work is performed.

## Maintained targets

- Host simulator and regression tests
- Raspberry Pi Pico using RP2040
- Waveshare RP2040-Zero compatibility variant using RP2040
- Raspberry Pi Pico 2 using RP2350 Cortex-M33
- Raspberry Pi Pico 2 using RP2350 Hazard3 RISC-V

The Pico 2 architecture presets remain compile/link smoke tests until RP2350
boot metadata, packaging, drivers, and hardware conformance are complete.

## Phase 1: portable engine and host reference

1. Define fixed-width values, graph limits, errors, and allocation policy.
2. Implement validation, deterministic execution, portable blocks, and runtime
   contracts.
3. Add deterministic host time, simulated I/O, fault injection, and tests.
4. Run strong warnings and supported sanitizers.

Exit criterion: the host executes a useful flow deterministically without
target-specific code in the library.

## Phase 2: firmware contracts

1. Define architecture, SoC, board, and capability interfaces.
2. Define the state guaranteed at the firmware entry point.
3. Provide initialization for data, BSS, panic, and compiler support.
4. Standardize linker symbols, map inspection, and size reporting.
5. Define reusable hardware-conformance tests.

Exit criterion: board firmware can integrate the portable engine without
changing it.

## Phase 3: Raspberry Pi Pico reference target

1. Maintain the FreeRTOS SMP boot path, static USB/status tasks, USB CDC
   console, reset support, and board-specific status output.
2. Define the task and core ownership required by the flow engine.
3. Integrate a bounded flow task and capability-controlled GPIO without
   coupling portable code to FreeRTOS.
4. Add watchdog ownership and measured deadline/overrun diagnostics.
5. Validate interrupt timing, resets, task stack margins, and sustained USB
   operation on both board variants.
6. Record flash, RAM, cycle-time, and jitter measurements.

Exit criterion: Pico executes a useful flow under FreeRTOS and passes hardware
conformance and an extended runtime test without drift, memory growth, stack
exhaustion, or watchdog failure.

The Waveshare RP2040-Zero compatibility preset reuses this target while
selecting its GPIO16 WS2812 status LED at compile time. Hardware evidence for
the Pico does not substitute for RP2040-Zero hardware validation.

## Phase 4: Raspberry Pi Pico 2

1. Implement RP2350 as a distinct SoC layer.
2. Complete Cortex-M33 boot metadata, packaging, drivers, and conformance.
3. Complete Hazard3 RISC-V boot metadata, packaging, drivers, and conformance.
4. Run identical golden flows on host, Pico, Pico 2 Arm, and Pico 2 RISC-V.
5. Compare observable behavior, memory use, cycle time, and jitter.

Exit criterion: both Pico 2 architectures produce flashable images and pass the
same applicable conformance suite as Pico.

## Phase 5: integration features

After the target matrix is stable, add persistent flow storage, robust updates,
richer diagnostics, ADC, PWM, buses, USB, and communications only behind
explicit capabilities. Each feature must be removable at build time and retain
portable flow semantics.
