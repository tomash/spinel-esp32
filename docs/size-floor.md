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
