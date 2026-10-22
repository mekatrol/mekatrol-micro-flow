# Mekatrol Micro Flow

**A lightweight, portable, flow-based automation engine for embedded systems.**

Mekatrol Micro Flow is a C library designed to execute modular automation and control logic on microcontrollers and resource-constrained devices.

The library provides a flexible foundation for building automation systems using interconnected function blocks, enabling developers to define and execute control logic without hardcoding every operation into application firmware.

Designed for portability, efficiency, and minimal hardware dependencies, Mekatrol Micro Flow can be integrated into a wide range of embedded applications, from small 8-bit and 32-bit microcontrollers to more capable IoT devices.

## Features

- **Lightweight:** Designed for microcontrollers with limited RAM, flash memory, and processing power.
- **Portable C:** Suitable for integration into different embedded toolchains and architectures.
- **Flow-Based Execution:** Execute automation logic through interconnected function blocks.
- **Modular Architecture:** Build complex automation systems from reusable logic components.
- **Hardware Independent:** Separate flow execution from platform-specific hardware functionality.
- **Flexible Execution:** Support cyclic execution of automation flows.
- **Extensible:** Allow additional function blocks and hardware interfaces to be implemented.
- **Standalone:** No external automation server or cloud infrastructure required.
- **Non-Commercial Friendly:** Free to use, modify, and share for permitted non-commercial purposes.

## Supported Platforms

Mekatrol Micro Flow is intended to support a wide range of embedded platforms.

| Platform | Architecture | Target |
|---|---|---|
| Raspberry Pi Pico / Pico W | ARM Cortex-M0+ (RP2040) | Planned |
| Raspberry Pi Pico 2 / Pico 2 W | ARM Cortex-M33 / RISC-V (RP2350) | Planned |
| ESP32 | Xtensa / RISC-V | Planned |
| CH32V003 | RISC-V | Planned |
| STM32 | ARM Cortex-M | Planned |
| Generic Embedded Systems | Platform-dependent | Planned |

The engine is designed to remain independent of any particular microcontroller, SDK, RTOS, or hardware abstraction framework.

Platform support and hardware requirements will depend on the available memory, processing resources, and implemented platform interfaces.

## Flow Engine

At the core of Mekatrol Micro Flow is a flow execution engine responsible for evaluating interconnected function blocks.

Each function block performs a specific operation, such as evaluating a condition, performing a calculation, processing a signal, or controlling an output.

Function blocks are connected through inputs and outputs, allowing the result of one operation to influence subsequent operations.

### Flow Execution

A typical flow execution cycle consists of:

1. Reading input values from connected sources.
2. Evaluating the configured function blocks.
3. Propagating results through connected blocks.
4. Updating output values.
5. Repeating execution according to the application schedule.

The execution model is intended to support deterministic, resource-efficient operation on embedded devices.

## Function Blocks

The engine is designed to support a range of automation and control functions.

### Logic Operations

- AND
- OR
- NOT
- XOR
- NAND
- NOR

### Comparison Operations

- Equal
- Not Equal
- Greater Than
- Greater Than or Equal
- Less Than
- Less Than or Equal

### Mathematical Operations

- Addition
- Subtraction
- Multiplication
- Division
- Minimum and Maximum
- Value Limiting

### Control and Signal Processing

- Conditional selection
- Value mapping
- Threshold evaluation
- State-based logic
- Signal conversion

### Timing and Sequencing

- On-delay timers
- Off-delay timers
- Pulse timers
- Counters
- Sequence control

### Inputs and Outputs

- Digital inputs and outputs
- Analog inputs and outputs
- Constant values
- Internal variables
- Hardware-independent data points

The function block categories above describe the intended capabilities. Individual operations will be made available as development progresses.

## Architecture

Mekatrol Micro Flow is designed around a modular architecture that separates automation logic from the underlying hardware.

### Core Engine

Responsible for:

- Flow initialization and lifecycle management.
- Function block execution.
- Input and output value handling.
- Connection evaluation.
- Flow state management.
- Execution scheduling integration.

### Function Block Library

Provides reusable operations that can be combined to implement automation and control logic.

Each block has a defined operation and associated inputs, outputs, and configuration parameters.

### Platform Abstraction

Provides integration points for hardware-dependent functionality, including:

- Digital and analog I/O.
- System time and timers.
- Memory management.
- Platform initialization.
- Application-specific data sources.

### Application Layer

The host firmware is responsible for configuring flows, providing hardware interfaces, and invoking the flow engine.

This separation allows the same flow logic to be reused across different hardware platforms with minimal changes.

## Example Applications

Mekatrol Micro Flow can be used in a variety of embedded automation projects.

**Industrial Automation**

Implement configurable control sequences, interlocks, conditional operations, and equipment monitoring.

**Irrigation Control**

Build irrigation controllers using schedules, sensor conditions, valve control, and flow-based decision logic.

**Smart Home Automation**

Create configurable automation for lighting, ventilation, environmental monitoring, and other connected devices.

**IoT Devices**

Process sensor data, evaluate conditions, and execute local automation without relying on cloud services.

**Embedded Controllers**

Develop custom controllers with configurable logic instead of fixed, application-specific control routines.

## Integration

Mekatrol Micro Flow is intended to be integrated directly into an embedded firmware project as a C library.

Depending on the target platform and build system, integration may involve:

1. Adding the library source and header files to the firmware project.
2. Configuring the required platform abstraction interfaces.
3. Defining or loading the automation flow.
4. Initializing the flow engine.
5. Invoking the engine from the application's main loop or scheduler.

Detailed build instructions, API documentation, and working examples will be added as the implementation develops.

## Design Principles

### Minimal Resource Usage

The library aims to minimize memory and CPU overhead, making it suitable for resource-constrained embedded devices.

### Hardware Independence

The core engine should not depend directly on platform-specific GPIO, timers, communication interfaces, or SDK functionality.

### Predictable Execution

Flow execution should be suitable for applications requiring consistent and predictable control behavior.

### Extensibility

New function blocks and platform interfaces should be easy to add without modifying the core execution architecture.

### Reusability

Automation logic should be portable between supported devices wherever the required function blocks and hardware interfaces are available.

### Standalone Operation

The library should be capable of executing automation logic locally without requiring external servers, cloud connectivity, or additional runtime services.

## Project Status

**Early Development**

Mekatrol Micro Flow is currently under development.

The initial development focus includes:

- Core flow execution engine.
- Function block definitions.
- Flow connection and value propagation.
- Basic logic and mathematical operations.
- Embedded-friendly memory management.
- Platform-independent execution interfaces.
- Example integrations for supported microcontrollers.

APIs, internal structures, and supported functionality may change during early development.

## Contributing

Contributions, bug reports, suggestions, and improvements are welcome.

Developers are encouraged to:

- Report issues and unexpected behavior.
- Suggest additional function blocks.
- Improve portability and performance.
- Contribute platform integrations.
- Improve documentation and examples.
- Submit pull requests with fixes and enhancements.

All contributions must comply with the project's licensing terms.

Contributors should be aware that commercial licensing is managed separately by the project copyright holder. Contribution licensing arrangements may be required before code is merged.

## License

Mekatrol Micro Flow is distributed under a **non-commercial source-available license**.

### Non-Commercial Use

The project is licensed under the [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial