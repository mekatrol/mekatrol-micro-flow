# Mekatrol Micro Flow

This repository contains the independently built firmware projects used by the
Mekatrol Micro Flow system.

## Projects

### [Core Controller](core-controller/README.md)

The primary flow controller and portable Micro Flow engine. It is written in C
and built with CMake.

```sh
cd core-controller
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

### [Triac Output Expander](triac-output-expander/README.md)

Firmware for the independent eight-channel triac output expansion board. It is
a Rust `no_std` project and does not share code or build tooling with the core
controller.

```sh
cd triac-output-expander
cargo test
```

Each project owns its source, documentation, tests, dependencies, and build
configuration. System-wide documentation, such as the board communication
protocol, may be added at the repository root when it is defined.
