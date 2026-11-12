# Spinel footprint floor on a 32-bit MCU (measured)

Measured 2026-11-08 on ESP32-C3 (riscv32, `-Os`, `--gc-sections`), spinel
pin `6604788e`, with `tools/size-report.sh`. Purpose: find what any port
to a non-ESP MCU would have to fit, so the ESP-IDF share is reported apart
and should be ignored when thinking about other chips.

```
tools/size-report.sh app/size/empty.rb app/size/int_only.rb app/blink.rb app/hello.rb
```

## Results (bytes)

| app | runtime flash | runtime .data+.bss | app TU flash | app TU .bss | ESP-IDF flash (ignore) |
|---|---:|---:|---:|---:|---:|
| `size/empty.rb` (`x = 1`) | 51 500 | 47 851 | 7 418 | 5 460 | 337 439 |
| `size/int_only.rb` (fib loop) | 162 573 | 48 203 | 18 146 | 6 484 | 370 120 |
| `blink.rb` (GPIO via FFI) | 162 713 | 48 203 | 26 397 | 6 484 | 374 378 |
| `hello.rb` (strings, puts) | 161 463 | 48 203 | 19 800 | 6 484 | 370 494 |

So the **spinel-owned floor** is:

- **~60 KB flash / ~53 KB static RAM** for a program that does nothing;
- **~180–190 KB flash / ~55 KB static RAM** as soon as it does anything
  real. blink, hello and an integer loop all land in the same place.

Not included: the GC heap (malloc'd at runtime; the C3 GC thresholds are
8 KB objects + 4–8 KB strings) and the Ruby task's stack.

## Where the static RAM goes (runtime, all apps)

| symbol | bytes | knob (`firmware/main/CMakeLists.txt` + `tools/build-rt.sh`) |
|---|---:|---|
| `sp_gc_roots` | 16 384 | `SP_GC_STACK_MAX=4096` |
| `sp_alloc_stats` | 12 288 | `SP_ALLOC_STATS=512` |
| `sp_alloc_names` | 4 096 | (alloc reporter) |
| `sp_gc_pinned` | 4 096 | `SP_GC_PINNED_MAX=1024` |
| `sp_gc_remembered` | 4 096 | `SP_GC_REMEMBERED_MAX=1024` |
| `sp_slab_wk` | 2 432 | slab allocator |
| `sp_str_shape` | 2 048 | `SP_STR_SHAPE_MAX=512` |
| `sp_str_lcache` | 1 536 | string cache |

App TU: `sp_exc_stack` + `sp_catch_stack` 2 432 each (`SP_EXC_STACK_MAX=8`,
`SP_CATCH_STACK_MAX=8`; one riscv `jmp_buf` frame is ~300 B), plus 1 KB
each of proc args and dynamic symbols.

Almost all of it is a compile-time capacity macro that is already
plumbed through. A "small MCU" profile would be a config change, not a
code change: alloc stats off, GC roots at 512, pinned/remembered at 128.
That estimates to roughly **10–12 KB static RAM**, which still needs
validating on hardware: a GC-roots overflow is a hard failure.

## Why "anything real" costs +110 KB flash

From the map's "Archive member included ... (symbol)" section, for
`int_only.rb`:

1. **Bignum auto-promotion.** `c = a + b` accumulated in a loop is
   promoted to Bignum by design (spinel README, "self-referential addition
   auto-promote to bigint"). The fib probe's locals were emitted as
   `sp_bigint_new_int(...)`, so `sp_bigint.o` (23 KB) and the float/libm
   surface it pulls (`frexp`, `ldexp`, `sp_dtoa.o` 19 KB) come in. On a
   32-bit target this pattern is common (counters, accumulators).
2. **The allocation reporter.** `sp_tu_init` registers a scan function for
   every runtime container type (`sp_alloc_report_tag(sp_Dir_scan, "Dir")`,
   Hash, Proc, OpenStruct, ...) behind a *runtime* `if
   (sp_alloc_report_on)`. Those references keep every type's scan code
   (and through it `sp_cold.o` → `sp_io.o` → `sp_net.o`, `sp_str_cold.o`,
   `sp_inspect.o`, ...) alive in every program. A compile-time switch
   would let `--gc-sections` drop them.
3. **Error paths.** Exceptions and inspect/format pull `sp_format.o`,
   `sp_time.o` and libm (`exp`, `pow`, `cos`, `sqrt`).

Largest runtime flash objects (int_only): sp_bigint 23 K, sp_str_cold
20 K, sp_dtoa 19 K, sp_slab 17 K, sp_gc 15 K, sp_alloc 12 K, sp_exc 11 K,
sp_poly_cold 8.5 K, sp_hash 6 K, sp_cold 5.8 K, sp_inspect 5.2 K,
sp_re 5.1 K.

## What this means for other MCUs

| class | flash / RAM | verdict with today's runtime |
|---|---|---|
| RP2350, STM32F4/F7/H7, nRF52840, i.MX RT, SAMD51 | ≥512 KB / ≥128 KB | fits today; RAM fine once knobs are set |
| STM32F411, SAMD21-256K | 256–512 KB / 32–128 KB | fits flash; needs the static-RAM diet |
| STM32F103, STM32G0 | 64–128 KB / 8–20 KB | no: 180 KB flash floor; needs (2) as a compile-time switch, plus bigint opt-out |
| ATmega328P / 2560 | 32–256 KB / 2–8 KB | no; see the no-runtime profile idea in the workshop notes |

Upstream asks this suggests, cheapest first:

- (a) `SP_ALLOC_REPORT` compile-time off, so `sp_tu_init` emits no tags;
- (b) a `--int-overflow=wrap|raise` mode that never promotes to Bignum;
- (c) a documented small-target capacity preset.

## Follow-up: the switches and the `small` preset (2026-11-08)

Spinel moved fast: the measurements above are at the old pin (`6604788e`);
everything below is spinel master `206e90c4` plus the `size-floor-switches`
branch (tomash/spinel), which adds:

- `SP_NO_ALLOC_REPORT`: compiles the allocation report out of the runtime
  and fences the generated `sp_tu_init` tags (the constructor that read
  `SPINEL_ALLOC_REPORT` was linked into every program);
- `--no-bigint`: loop accumulators stay Integer and raise on overflow; a
  value that could only be a Bignum (`0xdeadbeef` on 32-bit, `2**40`) is a
  compile error at its line;
- `SP_FIBER_STACK_CACHE_MAX`: the fiber stack cache's static table was sized
  for 1024 slots regardless of the 32 it uses.

On this repo's side: `presets/*.defs` is now the single list of capacity
macros, read by both `tools/build-rt.sh` and the firmware CMake (they used to
be duplicated by hand); `presets/small.defs` is the diet. Variant knobs:
`SPINEL_SRC`, `SPINEL_PRESET`, `SPINEL_FLAGS` (see `tools/size-report.sh`).

```
export SPINEL_SRC=~/spinel-size-floor
SPINEL_PRESET=small tools/build-rt.sh
VARIANT=small SPINEL_PRESET=small SPINEL_FLAGS=--no-bigint \
  tools/size-report.sh app/blink.rb app/wifi_status.rb
```

### Spinel runtime on ESP32-C3: flash / static RAM (.data+.bss), bytes

| app | master, default preset | + switches | `small` preset + `--no-bigint` |
|---|---:|---:|---:|
| `size/empty.rb` | 61 500 / 51 763 | 59 698 / 35 355 | 59 740 / **11 083** |
| `size/int_only.rb` | 65 912 / 51 763 | 60 086 / 35 355 | 60 128 / **11 083** |
| `blink.rb` | 68 729 / 51 763 | 65 813 / 35 355 | 65 861 / **11 083** |
| `hello.rb` | 172 613 / 52 115 | 169 517 / 35 707 | 169 725 / **11 435** |
| `wifi_status.rb` | 344 515 / 98 463 | 341 251 / 82 055 | 341 693 / **32 007** |

"+ switches" = default preset with `SP_NO_ALLOC_REPORT` and `--no-bigint`.
The generated app TU's static RAM drops from 2 644 to 1 876 bytes under
`small` (catch stack 8 → 4).

Reading it:

- **Upstream already halved the flash floor.** At the old pin an integer
  loop pulled 162 KB of runtime; on master it is 66 KB even with the default
  build, because codegen stopped referencing whole families of runtime code
  it does not use. The flash numbers are now dominated by what the program
  actually does (hello's string formatting: +100 KB; networking: +270 KB).
- **`--no-bigint` is worth ~6 KB flash** on master (int_only 65.9 → 60.1 KB):
  much less than at the old pin, but it also turns a silent Bignum into a
  loud `RangeError` or a compile error, which is the behaviour an MCU wants.
- **Static RAM: 51.8 → 11.1 KB for ordinary apps, 98.5 → 32.0 KB for the
  WiFi server.** The reporter (16.4 KB), the GC roots fast region (14 KB),
  the remembered/pinned sets (7.7 KB), the scheduler's per-worker tables
  (`g_lrq`, 16 KB in wifi_status) and the fiber stack cache (8 KB).

What is left in wifi_status's 32 KB: `sp_hdr_char_cache` 16 KB (a fixed
`[2][256]` table of one-character Strings, built on first use; a lazy or
smaller variant is the next upstream candidate), exception and catch
stacks 3.6 KB, slab worker 2.4 KB, GC roots 2 KB, string-length cache
1.5 KB, regexp and backtrace buffers 1 KB each.

### Is the diet safe? (host evidence)

`tools/preset-stress.sh small` rebuilds the runtime with the preset on the
host and runs spinel's GC torture corpus against it: every
`# spinel: gc-stress` test under `SPINEL_GC_STRESS=2` (a collection at every
allocation) with and without `SPINEL_GC_VERIFY`, and every
`# spinel: gc-minor` test with the generational mark on, where 32-entry
remembered/pinned sets overflow constantly. Result of a clean full run:
**691 pass, 0 fail, 61 skipped** (752 runs). The skipped ones are 34
programs that need the threaded runtime (`-DSP_THREADS`, which no MCU build
links); the harness recognises them from the driver's build plan.

### On silicon: T-OI Plus (ESP32-C3), 2026-11-08

Both apps built with `SPINEL_PRESET=small SPINEL_FLAGS=--no-bigint` against
the `size-floor-switches` tree, flashed over `/dev/ttyUSB0`:

- **blink.rb**: 10 blinks in the expected 4 s, `main()` returned 0.
- **wifi_status.rb**: associates, gets its IP, serves `/`, `/led/toggle`,
  `/ota/status`; mDNS (`spinel-mote.local`) resolves.
  - **RST storm**: two rounds of 300 connections closed with `SO_LINGER=0`
    (half before sending a request): 103 `accept error: ECONNABORTED` lines
    logged and rescued, zero panics, 40/40 normal GETs OK afterwards. Heap
    dipped to 32.3 KB free and recovered.
  - **OTA**: forced round trip from a LAN `python3 -m http.server`:
    1 454 272 B streamed into `ota_1` in 27 s (minimum free heap 45 KB),
    sealed, rebooted into `ota_1`, proof-of-life GET, `marked valid
    (rollback cancelled)`. One earlier attempt dropped its connection
    partway, with no serial capture running; it could not be reproduced.

**Free heap at idle, same app, same spinel tree, same board:**

| preset | free heap | largest block |
|---|---:|---:|
| default | 35 580 B | 24 576 B |
| `small` + `--no-bigint` | 102 888 B | 90 112 B |

The +67 KB is the static RAM the preset removed (98.5 -> 32.0 KB), now
available to the IDF heap.

**Porting bug found on the way (not the preset):** spinel master
(820b9a6a6) makes stdout unbuffered when it is a tty. ESP-IDF's stdout is one
stream shared by every task, and newlib prints unbuffered output through a
1 KB stack buffer (`__sbprintf`), so the 2304-byte `sys_evt` task overflowed
on its first log line after the IP was assigned: a stack-protection panic on
every boot. `shim/esp32/include/stdio.h` now keeps stdout line-buffered.

### Pin moved to spinel master (2026-11-08, branch pin/spinel-master)

`vendor/spinel` now points at upstream `4cb57256`. On the T-OI Plus with the
default preset: hello, blink, battery, i2c and wifi_status run; wifi_status
passes the RST storm and three consecutive OTA round trips (ota_0 -> ota_1
-> ota_0 -> ota_1, ~28 s each, all marked valid), idling at 90.6 KB free.
The old default sizes were not enough on master: OTA died with
`NoMemoryError` (upstream added 16 KB read buffers per socket and 256-worker
scheduler tables), so the diet values became `presets/default.defs`.
`wifi_status.rb` also gained a GC checkpoint every 8 OTA chunks.

The ESP32 (Xtensa) apps build and link (after anchoring `__wrap_longjmp`,
see `firmware/main/CMakeLists.txt`) but were not run: no T-Display was
attached.

### Pin at ca4b6ec8f (2026-11-10): the switches are upstream

matz/spinel#11513 merged the three switches (`SP_NO_ALLOC_REPORT`,
`--no-bigint`, `SP_FIBER_STACK_CACHE_MAX`), so `presets/small.defs` no longer
needs a fork: the pin moved to `ca4b6ec8f`, where the small runtime archive
has no reporter symbols and the vendored compiler takes `--no-bigint`. All
apps build for both targets (C3: blink, wifi_status with default and small;
ESP32: the three T-Display demos and wifi_status). On the T-OI Plus with the
default preset (2026-11-11): wifi_status idles at 90.3 KB free, survives the
300-connection RST storm (20/20 GETs after) and an OTA round trip into ota_1,
marked valid. Not re-run at this pin: the small preset on hardware, and any
ESP32/Xtensa board. `small` stays opt-in.

### Still open

- **T-Display on hardware**: flash blink_tdisplay, buttons_tdisplay,
  display_tdisplay and wifi_status on the classic ESP32.
- **Prebuilt runtime release** for the new pin (`tools/release-assets.sh
  publish`): `make setup` downloads assets for the pinned commit.
- **`/led/toggle` does not toggle**: the toggle code was dropped from
  `wifi_status.rb` in the M6 OTA commit (`bfb4a05`); unrelated to this work.
- **Upstream follow-ups**: an override for `SP_IO_RBUF_MIN` (16 KB per
  buffered stream) and a lazy `sp_hdr_char_cache` (16 KB static).
