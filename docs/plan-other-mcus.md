# Beyond ESP32: plan for other microcontroller targets

Workshop notes, 2026-11-08. Companion docs: `docs/size-floor.md` (measured
footprint), `docs/hardware-shopping-list.md` (boards to buy).

## Premise

Spinel compiles Ruby to plain C, so in principle any chip with a C compiler
is a target. In practice the generated C always links against
`libspinel_rt`: GC, boxing, class hierarchy, exceptions, strings. Even a
blink program calls into it. So for any chip the question is **"can it host
the runtime?"**, not "does it have a C compiler?"

What the runtime requires, as learned from the ESP32 port
(`docs/findings.md`):

| Requirement | Where it bites |
|---|---|
| `intptr_t` of 32 or 64 bits (`#error` otherwise in `sp_types.h`) | rules out 8/16-bit AVR, MSP430 |
| `malloc`, `setjmp`/`longjmp`, stdio | newlib or newlib-nano is enough |
| Fiber context switch (`sp_ctx_swap`/`sp_ctx_make`) | x86_64, aarch64 upstream; riscv32 in `shim/esp32/src/ucontext.S`; anything else needs ~40 lines of asm or a no-fiber mode |
| POSIX header surface (signal, poll, mman, socket…) | covered by shim headers + `--gc-sections`, as in `shim/esp32/` |
| Startup adapter (`main` → task/entry point) | per platform, small |
| Footprint: ~180–190 KB flash, ~55 KB static RAM today | gates the small-chip tiers; see `docs/size-floor.md` |

Good news: arm-none-eabi, Pico SDK, STM32Cube, Zephyr and most RISC-V
vendor SDKs all ship newlib, the same libc ESP-IDF uses. Most of
`shim/esp32/` should carry over unchanged.

## Target tiers

### Tier 1: reuse almost everything (weekend-sized ports)

| Target | Core | Flash / RAM | Why it's easy |
|---|---|---|---|
| ESP32-C6, H2, P4 | RISC-V | 4 MB+ / 320–768 KB | same toolchain family, same `ucontext.S` |
| ESP32-S3 | Xtensa LX7 | 4–16 MB / 512 KB (+PSRAM) | same Xtensa path as the classic ESP32 port |
| **RP2350 (Pico 2 / 2 W)** | Cortex-M33 **or** Hazard3 RISC-V | 4 MB / 520 KB | Pico SDK is CMake + newlib like ESP-IDF; RISC-V mode reuses `ucontext.S`; Pico 2 W networking is lwIP, so `sp_lwip_compat.c` and the socket fixes transfer |
| RP2040 (Pico / Pico W) | Cortex-M0+ | 2 MB / 264 KB | same SDK; needs Thumb-1 context switch |
| CH32V307, BL616/BL602, GD32VF103 | RISC-V | 128 KB–4 MB / 32–480 KB | `ucontext.S` reusable; GD32VF103 (128K/32K) is Tier 3 by size |

**First recommended non-Espressif target: Pico 2 W in RISC-V mode.** No new
assembly, and the networking layer is the same lwIP. Then flip the same
board to Cortex-M33 to bootstrap the ARM port on known-good hardware.

### Tier 2: Cortex-M port (one assembly file unlocks all of them)

| Target | Flash / RAM | Notes |
|---|---|---|
| STM32F4 (F411, F446, F429) | 512 KB–2 MB / 128–256 KB | F411 is the "tight" reference |
| STM32F7 / H7 | 1–2 MB / 512 KB–1 MB | H7 is the "roomy" reference, Ethernet |
| Teensy 4.x (i.MX RT1062) | 2–8 MB / 1 MB | 600 MHz, very comfortable |
| nRF52840 | 1 MB / 256 KB | BLE instead of WiFi; Zephyr-first |
| SAMD51 (Adafruit M4) | 512 KB / 192 KB | Arduino ecosystem |

Work: `sp_ctx_swap`/`sp_ctx_make` for Thumb-2 (save r4–r11, sp, lr; plus
s16–s31 when the FPU context is live), a startup adapter, UART retarget for
stdout, `_sbrk` or RTOS heap.

### Tier 3: needs a runtime diet

| Target | Flash / RAM |
|---|---|
| STM32F103 "Blue Pill" | 64–128 KB / 20 KB |
| SAMD21 (Arduino Zero/MKR, XIAO) | 256 KB / 32 KB |
| STM32G0 / L0, nRF52832 | 64–512 KB / 8–64 KB |

Blocked today by the ~180 KB flash floor and ~55 KB static RAM. Unblocked by:

1. **Static-RAM preset**: capacity macros already exist (`SP_GC_STACK_MAX`,
   `SP_ALLOC_STATS`, `SP_GC_PINNED_MAX`, `SP_GC_REMEMBERED_MAX`,
   `SP_EXC_STACK_MAX`…). Estimated ~10–12 KB static RAM. Config only.
2. **Compile-time alloc-reporter switch** (upstream): `sp_tu_init` registers a
   scan function for every container type behind a *runtime* flag, which
   keeps Dir/Hash/Proc/IO/net/inspect code alive in every program.
3. **No-Bignum integer mode** (upstream): `c = a + b` in a loop auto-promotes
   to Bignum by design, pulling in ~23 KB bigint + ~19 KB dtoa + libm.
   A `wrap`/`raise` mode would drop it.

### Tier 4: AVR / ATmega (different approach entirely)

| Target | Flash / RAM |
|---|---|
| ATmega328P (Uno, Nano) | 32 KB / 2 KB |
| ATmega2560 (Mega) | 256 KB / 8 KB |
| ATmega32U4 (Leonardo, Pro Micro) | 32 KB / 2.5 KB |

Porting the runtime is not realistic:

- **16-bit pointers.** `sp_types.h` refuses to compile. Forcing a 32-bit
  `sp_int` breaks the integer/pointer sharing in the value union.
- **RAM.** 2–8 KB total vs ~55 KB static runtime state, before any heap.
- **Harvard architecture.** String literals are emitted as static structs in
  `.rodata`, which avr-gcc copies to RAM at startup unless placed in
  `PROGMEM`. The runtime would need `pgm_read_*` on every literal access.
- **`double` is 32-bit** by default in avr-gcc; Float semantics silently differ.

The realistic path is a **no-runtime profile in spinel itself**: a compiler
mode that rejects anything needing GC or heap allocation (non-literal
Strings, Arrays, Hashes, exceptions, Bignum) and only accepts programs
where inference proves every value is an unboxed integer, constant or FFI
call. The output is then plain C with no `libspinel_rt`. That would make
blink on an Uno possible, and would also serve Tier 3. It is a real
compiler feature and needs upstream buy-in: write a proposal before code.

## Platform strategy: per-SDK ports vs Zephyr

| Option | Pros | Cons |
|---|---|---|
| Per-vendor SDK (Pico SDK, STM32Cube/bare, nRF SDK) | small, transparent, mirrors the ESP-IDF port | one shim + adapter per vendor |
| **Zephyr** | one port → hundreds of boards (STM32, nRF, RP2040, ESP32, RISC-V); `CONFIG_POSIX_API` supplies poll, sockets and pthreads, retiring much of the shim | heavier build (west, devicetree, Kconfig); larger baseline |
| Arduino cores (arduino-pico, STM32duino) | huge library ecosystem reachable via FFI | C++: `ffi_source` adapters need `extern "C"`; Arduino's `setup`/`loop` shape vs spinel's `main` |

Recommendation: do the Pico SDK port by hand first, to learn what a
non-ESP port really needs. Then evaluate Zephyr as the scaling path,
rather than writing N vendor ports.

## Roadmap

| # | Milestone | Hardware | Exit criterion |
|---|---|---|---|
| 1 | ✅ Measure footprint floor | none (ESP32-C3 build) | `docs/size-floor.md` |
| 2 | 🔶 Static-RAM diet preset: `presets/small.defs` + spinel `size-floor-switches`; 11 KB (blink) / 32 KB (wifi_status) measured, GC torture-tested on host; silicon run pending | T-OI Plus (C3) | blink + wifi_status run on silicon with ≤12 KB runtime static RAM (blink) |
| 3 | Pico 2 W, RISC-V mode | Pico 2 W, Debug Probe | blink + hello over UART |
| 4 | Pico 2 W networking | Pico 2 W | wifi_status serving HTTP over CYW43/lwIP |
| 5 | Cortex-M context switch | Pico 2 W (M33 mode), Nucleo-F411RE | Fiber test passes on M33 and M4F |
| 6 | STM32 bring-up | Nucleo-F411RE, Nucleo-H743 | blink, I2C scan; size report for the F411 |
| 7 | 🔶 Upstream asks: `SP_NO_ALLOC_REPORT`, `--no-bigint`, `SP_FIBER_STACK_CACHE_MAX` on tomash/spinel `size-floor-switches` (not yet a PR); next: lazy `sp_hdr_char_cache` | n/a | PRs merged upstream |
| 8 | Tier 3 attempt | Blue Pill, XIAO SAMD21 | blink fits in 64 KB / 20 KB |
| 9 | Zephyr evaluation | nRF52840 + an existing board | same app on two vendors from one port |
| 10 | No-runtime profile proposal + prototype | Arduino Uno | blink.rb → plain C → AVR, no libspinel_rt |

## Challenges and risks

**Runtime / compiler**
- **Upstream dependency.** Steps 7, 8 and 10 need changes in matz/spinel.
  The two ESP32 PRs merged quickly, but a no-runtime profile is a design
  change, not a fix. Mitigation: proposals with measurements first.
- **Bignum promotion.** Even if a no-promotion mode lands, Ruby integer
  semantics on 32-bit (overflow at 2³¹) will surprise users. It needs to
  be loud: raise, not wrap, by default.
- **GC roots capacity.** Shrinking `SP_GC_STACK_MAX` trades RAM for a hard
  failure when a deep call chain overflows it. Needs a stress test per preset.
- **Prebuilt runtime matrix.** Each core/ABI/FPU combination (rv32imc,
  rv32imac, Xtensa LX6/LX7, M0+, M33, M4F, M7 + capacity preset) is another
  `libspinel_rt` variant to build, cache and publish via `release-assets.sh`.

**Platform**
- **Context-switch assembly per ISA.** Thumb-1 (M0+) lacks the
  instructions Thumb-2 has. FPU lazy stacking on M4F/M33/M7 complicates
  which registers to save. Fallback: a no-fiber runtime variant.
- **Stack sizing without an MMU.** No guard pages anywhere outside Linux:
  a stack overflow corrupts memory silently. Use MPU stack guards where
  available (M33, M7) or canaries.
- **Interrupts.** Ruby state belongs to one task. ISRs must stay in C
  (`ffi_source`) and hand off via queues/flags. This needs a documented
  pattern before users try Ruby callbacks from an ISR.
- **Flash-resident data.** On XIP-flash parts (RP2xxx, ESP32) `.rodata`
  reads are cached and fine; on some STM32s and on AVR, placement matters.
- **Toolchain installs.** Only Espressif toolchains are installed today:
  `arm-none-eabi-gcc`, a generic `riscv32` GCC, `avr-gcc`, openocd and
  probe-rs are all needed. `tools/bootstrap.sh` must grow per-target setup.

**Project**
- **Scope creep.** Each tier is a project on its own. Keep a tier closed
  (exit criterion met on silicon) before opening the next.
- **Testing.** Board count grows linearly with tiers; a HIL (hardware in
  the loop) setup with a powered hub and per-board `/dev/serial/by-id`
  paths will be needed by milestone 6 (ttyUSB ordering is already unstable).
- **Naming.** "spinel-esp32" stops describing the repo after milestone 3.
  Decide on a rename or a sibling repo before publishing non-ESP work.
