# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal firmware for the Raspberry Pi Pico 2 (RP2350, ARM Cortex-M33), built with Pico SDK 2.2.0. The project name is `opl3` — likely an OPL3 FM synthesis implementation target.

## Build

The build directory is pre-configured. To compile:

```sh
ninja -C build
```

To rebuild from scratch (reconfigure first):

```sh
cmake -S . -B build -G Ninja
ninja -C build
```

Build outputs land in `build/`: `opl3.elf`, `opl3.uf2`, `opl3.elf.map`.

## Flashing

Flash via picotool (USB):

```sh
picotool load -fx build/opl3.uf2
```

Flash via SWD/OpenOCD (CMSIS-DAP probe):

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "program build/opl3.elf verify reset exit"
```

## Debugging

Two launch configs exist in `.vscode/launch.json`:
- **"Pico Debug (Cortex-Debug)"** — launches OpenOCD automatically via the Cortex Debug extension
- **"Pico Debug (Cortex-Debug)(External)"** — expects OpenOCD already running on port 3333

## Toolchain

- Compiler: `arm-none-eabi-gcc` 14.2 (`/usr/bin/arm-none-eabi-gcc`)
- CMake 3.31+, Ninja 1.12+
- OpenOCD 0.12+ for SWD debugging/flashing
- Pico SDK path resolved via `PICO_SDK_PATH` env var or `pico_sdk_import.cmake`

## Architecture

Currently a single source file: [opl3.c](opl3.c). The build is configured as a `PICO_EXECUTABLE` target with C11/C++17, `-O3`, and function/data sections for dead-code elimination.

New source files must be added to `target_sources(opl3 ...)` in [CMakeLists.txt](CMakeLists.txt). Additional Pico SDK libraries (e.g., `hardware_pwm`, `hardware_i2s`) are linked via `target_link_libraries`.

The SDK header `pico/stdlib.h` pulls in `stdio`, `time`, and GPIO init. Hardware-specific headers (`hardware/pwm.h`, `hardware/dma.h`, etc.) are included separately as needed.
