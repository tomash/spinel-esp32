# Hardware shopping list for non-ESP32 targets

2026-11-08. Supports the roadmap in `docs/plan-other-mcus.md`. Prices are
rough USD street prices; buy from official distributors (Raspberry Pi
resellers, Mouser, DigiKey, LCSC, WeAct's official store) where possible.

Already on the bench: LilyGO T-OI Plus (ESP32-C3) and LilyGO T-Display
(classic ESP32). These remain the reference boards for the ESP-IDF path
and for milestone 2 (static-RAM diet).

## Priority 1: buy now (~$40, unblocks milestones 3–6)

| Item | Qty | ~Price | Milestone | Reasoning |
|---|---:|---:|---|---|
| **Raspberry Pi Pico 2 W** | 2 | $7 ea | 3, 4, 5 | Best first non-ESP target. RP2350 runs as RISC-V (reuses `ucontext.S`, no new asm) **or** Cortex-M33 (bootstraps the ARM port on the same board). WiFi via CYW43 + lwIP, the same stack the ESP32 socket work targeted. 4 MB / 520 KB, so footprint is a non-issue. The second unit is spare or a second probe. |
| **Raspberry Pi Debug Probe** | 1 | $12 | 3–6 | SWD (Serial Wire Debug) debugger plus UART in one, works with openocd and probe-rs for any Cortex-M. Faster than flashing via USB drag-and-drop. GDB will be needed for context-switch and stack bugs. |
| **ST Nucleo-F411RE** | 1 | $15 | 5, 6 | The "tight" Cortex-M4F reference: 512 KB / 128 KB. Exercises the FPU context-switch path and the static-RAM diet against a real limit. Built-in ST-LINK, so no extra probe. Arduino-header compatible. |

**Considerations**
- Get the **Pico 2 W**, not the original Pico W (RP2040, M0+ only, no RISC-V
  cores). An RP2040 is still useful later for the Thumb-1 case.
- Buy Pico 2 W **with headers (WH)** to save soldering, unless you plan to
  mount it on a carrier board.
- The Pico Debug Probe ships with the cables needed for the Pico's 3-pin
  debug connector. Nucleo boards need none.

## Priority 2: buy when milestone 5 (Cortex-M asm) works (~$50)

| Item | Qty | ~Price | Milestone | Reasoning |
|---|---:|---:|---|---|
| **ST Nucleo-H743ZI2** | 1 | $30 | 6 | The "roomy" Cortex-M7 reference (2 MB / 1 MB, Ethernet, double-precision FPU). Proves the port when RAM is not the problem, and gives wired networking to test the socket layer without WiFi. Also has an MPU for stack guard experiments. |
| **WeAct STM32F103C8 "Blue Pill Plus"** | 2 | $4 ea | 8 | The below-the-floor target (64 KB / 20 KB) that justifies the diet and upstream work. WeAct boards carry genuine ST chips; generic Blue Pills are often counterfeit (CKS/APM clones) and behave differently under debug. |
| **ST-LINK V3 MINIE** (or V2 clone) | 1 | $12 / $4 | 8 | Debug probe for boards without an onboard one. The V3 MINIE is genuine and reliable; V2 clones work but have flaky firmware. The Pico Debug Probe also works. |

## Priority 3: second wave, new families (~$40–90, pick by interest)

| Item | Qty | ~Price | Milestone | Reasoning |
|---|---:|---:|---|---|
| **ESP32-C6-DevKitC-1** | 1 | $9 | n/a (cheap win) | Same toolchain, RISC-V, WiFi 6 + Thread/Zigbee. Nearly free to add, and grows the supported list. |
| **ESP32-S3-DevKitC-1 (N8R8)** | 1 | $15 | n/a (cheap win) | 8 MB PSRAM, which removes RAM pressure entirely. Good for showcase apps (displays, larger web servers). Xtensa LX7, close to the existing Xtensa port. |
| **WeAct CH32V307** + **WCH-LinkE** | 1 + 1 | $10 + $5 | Tier 1 RISC-V | Non-Espressif RISC-V (256 KB / 64 KB, Ethernet MAC). Proves the riscv32 path isn't tied to ESP-IDF. Needs WCH's openocd fork, which is a toolchain consideration. |
| **Seeed XIAO nRF52840** | 1 | $10 | 9 | Cheapest Zephyr-first target with BLE. The full **nRF52840 DK** ($50) has an onboard J-Link and is nicer for debugging; choose the DK if Zephyr becomes the main path. |
| **Seeed XIAO SAMD21** | 1 | $6 | 8 | Arduino Zero class (256 KB / 32 KB, Cortex-M0+). Second Tier 3 data point, plus the Thumb-1 context switch. |
| **Teensy 4.1** | 1 | $32 | optional | 600 MHz M7, 1 MB RAM (+PSRAM pads), Ethernet. Nice performance showcase; not needed for porting work. |

## Priority 4: AVR, only after a no-runtime prototype exists (~$15–30)

| Item | Qty | ~Price | Reasoning |
|---|---:|---:|---|
| Arduino Uno R3 (or quality clone) | 1 | $5–25 | ATmega328P, 32 KB / 2 KB: the canonical target for the no-runtime profile. |
| Arduino Mega 2560 (clone) | 1 | $15 | 256 KB / 8 KB: the "generous AVR" data point. |
| USBasp programmer | 1 | $3 | Flashing without the bootloader, which frees 0.5–2 KB of flash. |

Don't buy these until milestone 10 has code. Without the compiler work they
can't run anything from this project.

## Bench and infrastructure

| Item | ~Price | Priority | Reasoning |
|---|---:|---|---|
| **Powered USB hub (7+ ports, per-port switches)** | $25 | 1 | ttyUSB ordering is already unstable with two boards. With five or more, use `/dev/serial/by-id/` paths and power-cycle boards individually. Prerequisite for any hardware-in-the-loop testing. |
| **8-channel logic analyser (Saleae clone, fx2lafw)** | $10 | 1 | Verifying GPIO timing, I2C and SPI from Ruby without guessing. Works with PulseView/sigrok. |
| Breadboards, jumper wires (M-M, M-F), USB-C/micro-USB data cables | $15 | 1 | Charge-only cables waste hours; label the data ones. |
| A few I2C sensors (BME280, SSD1306 OLED) | $10 | 2 | Same peripherals across every board, so an `i2c.rb`-style app is a cross-platform smoke test. |
| **Nordic PPK2** power profiler | $100 | 3 | Only if low-power or sleep modes become a goal. Measures µA-level current per code path. |
| USB isolator | $20 | 3 | Protects the host when boards share external power supplies. |

## Budget summary

| Tier | Items | ~Total |
|---|---|---:|
| Priority 1 | 2× Pico 2 W, Debug Probe, Nucleo-F411RE | **$41** |
| + bench essentials | powered hub, logic analyser, wires/cables | **+$50** |
| Priority 2 | Nucleo-H743ZI2, 2× WeAct F103, ST-LINK | +$50 |
| Priority 3 | pick 2–3 boards | +$25–90 |
| Priority 4 | AVR set | +$25–45 (deferred) |

**Minimum viable start: ~$90** (Priority 1 + bench essentials) covers
RISC-V reuse, the Cortex-M port and the small-RAM limit, on a bench that
can keep track of which board is which.

## Toolchains to install alongside (no cost)

| Toolchain | For |
|---|---|
| `arm-none-eabi-gcc` (Arm GNU Toolchain 14.x) | all Cortex-M boards |
| Pico SDK + picotool | RP2350/RP2040 |
| `riscv32-unknown-elf` / Pico SDK RISC-V toolchain | RP2350 RISC-V mode, CH32V |
| openocd (upstream + WCH fork) and/or probe-rs | flashing and debugging via SWD |
| Zephyr SDK + west | milestone 9 |
| `avr-gcc` + avrdude | milestone 10 only |
