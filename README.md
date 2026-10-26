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

### [Output Expander](output-expander/README.md)

Firmware for the independent eight-channel output expansion boards, using the
WCH CH32V003 processor. Triac and FET hardware variants are supported by this
Rust `no_std` project, which does not share code or build tooling with the core
controller.

```sh
cd output-expander
cargo test
cargo test --lib --target x86_64-unknown-linux-gnu
```

Each project owns its source, documentation, tests, dependencies, and build
configuration. System-wide documentation, such as the board communication
protocol, may be added at the repository root when it is defined.
