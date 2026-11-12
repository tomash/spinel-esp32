# Fitting spinel into a CH32V203C8 (64 KB flash / 20 KB RAM)

Bare-metal measurement, no SDK, no RTOS: `tools/ch32-size.sh` emits the app
with spinel, compiles it for rv32imac at `-Os`, and links it with the
runtime, newlib-nano and libnosys (`baremetal/measure.ld`, `baremetal/crt0.S`).
Startup, vector table and a GPIO driver are not in these numbers yet
(estimate: 1–2 KB).

`app/ch32/blink.rb` toggles PA0 ten times through register writes (FFI).

## Steps (bytes; flash = text+data, RAM = data+bss)

| step | empty.rb flash | blink.rb flash | blink.rb RAM |
|---|---:|---:|---:|
| `small` preset, `--no-bigint`, runtime `-O2` | 77 984 | 93 416 | 14 840 |
| runtime `-Os` (`RT_OPT=-Os`) | 67 952 | 84 944 | 14 840 |
| no environment: `getenv` folded to NULL (`baremetal/noenv.h`) | 55 700 | 72 704 | 14 800 |
| `SP_NO_GC_PHASES` (no double GC clock) | 49 268 | 66 272 | 14 632 |
| GC minor cadence in fixed point | **47 244** | **64 240** | **14 632** |

blink now fits: 62.7 KB of 64 KB, leaving 5.8 KB of RAM for stack and GC heap.

```
SPINEL_SRC=~/spinel-size-floor   # branch mcu-trim
RT_OPT=-Os RT_DEFS="-include $PWD/baremetal/noenv.h -DSP_NO_GC_PHASES" \
  SPINEL_PRESET=small RT_SUFFIX=-trim2 tools/build-rt.sh riscv32imac
RT_DIR=build/rt-riscv32imac-trim2 tools/ch32-size.sh
```

## What each step removed

- **`-Os`**: about 9 KB across the runtime (slab, exceptions, GC).
- **Environment knobs**: the GC and slab read `SPINEL_GC_*` tunables with
  `getenv` + `atof`, which links newlib's `strtod` (with `mprec` and `gethex`,
  about 10 KB). With no environment on the chip, every knob reads unset and
  the compiler drops the parsers.
- **GC clock** (spinel `mcu-trim`): phase timing, total GC seconds and the
  `malloc_trim` pacing were kept as doubles; the soft-float add, mul and sub
  came with them.
- **Fixed-point cadence** (spinel `mcu-trim`, all builds): the one GC
  heuristic that divided doubles now uses 1/1024ths in integers.

## Next levers

- Flash: stdio/printf, about 5 KB (`vfprintf`, `fvwrite`, `fflush`), used by
  uncaught-exception and GC-diagnostic printing; `sp_poly_cold` 5 KB;
  `qsort` 0.9 KB; the generated class-name table (`sp_class_to_s`, about
  1.5 KB of strings).
- RAM: exception stack 2.4 KB (`SP_EXC_STACK_MAX=8`), the slab worker
  2.4 KB, the string-length cache 1.5 KB, backtrace buffer 1 KB, proc args
  1 KB.
