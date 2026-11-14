# Shrinking spinel for 64 KB parts: levers inspired by mruby/c

Starting point (`docs/ch32v203-size.md`): `app/ch32/blink.rb` links
bare-metal for rv32imac at **62.7 KB flash / 14.3 KB static RAM**, against
the CH32V203C8's 64 KB / 20 KB. Startup and a GPIO driver still have to fit
in the 1.3 KB that is left, and the stack and GC heap in the 5.8 KB of RAM.

## What mruby/c does differently (from its source, `~/src/mrubyc`)

mruby/c's README promises **< 40 KB of RAM** (it gives no flash figure).
It runs on 16-bit PICs. Its choices:

| mruby/c | spinel today | lever for spinel |
|---|---|---|
| Bytecode VM: fixed size, programs cost bytes of bytecode | native code per method, plus per-program tables | (not adopted: native speed is the point) trim generated tables |
| One fixed memory pool, TLSF + first fit (`alloc.c`, ~1 000 lines), optional 16-bit block headers | slab allocator built for many-core servers (`sp_slab`, 11.8 KB) | simpler allocator for MCU builds |
| Reference counting, no tracing collector | generational mark-sweep (`sp_gc`, 9.5 KB) | simple non-generational mark-sweep, or `--gc=none` for programs that allocate up front |
| Own `mrbc_printf` (`console.c`): %d %s %x %c, writes through `mrbc_hal_write` | newlib stdio: `vfprintf`, `fvwrite`, `fflush`, FILE tables (about 5 KB + RAM) | a tiny printf and write path |
| Exceptions are values the VM unwinds, no backtrace capture | setjmp stacks, a 1 KB backtrace buffer, uncaught printer (`sp_exc`, 11.5 KB) | MCU switch: no backtraces, short uncaught message |
| Feature switches in `vm_config.h` (`MRBC_USE_FLOAT`, `MRBC_USE_STRING`, `MRBC_USE_MATH`) | presets for capacities; features follow what the program uses | keep (spinel's codegen already drops unused classes' code) |
| No OS assumptions anywhere | environment tunables, clocks, `malloc_trim`, workers | done: `noenv.h`, `SP_NO_GC_PHASES` |

## The levers, in the order they are tried

Each is measured with `tools/ch32-size.sh` (empty.rb and blink.rb), one at a
time, kept if it pays and does not change behaviour the program can see.

0. **mruby/c baseline.** Link mruby/c's VM bare-metal with the same
   toolchain and flags (`baremetal/mrubyc/`) so "how big is mruby/c" is a
   measured number, not a README one. Done, see Results.
1. **Compiler flags.** `-msave-restore` (shared RISC-V prologue/epilogue
   routines) and LTO across the app and runtime. Cheap, no source changes.
2. **Tiny printf and output path.** Replace newlib's stdio for the
   runtime's few `fprintf`/`snprintf` uses with a small integer-only
   formatter writing to a board `putchar`, mruby/c-style. In this repo
   (link-time), no spinel change.
3. **RAM preset `tiny`.** Exception stack 8 → 4, then the fixed buffers
   that have no knob yet (backtrace 256 frames, string-length cache 128
   entries) as small upstream knobs.
4. **Allocator and collector.** First measure `--gc=none` (ceiling of what
   a simpler collector could save), then the slab: is there a malloc-backed
   path, and what does a plain mark-sweep over one pool cost. Biggest
   lever, biggest change; upstream design discussion before code.
5. **Exceptions.** An MCU switch that compiles out backtrace capture and
   the detailed uncaught printer.
6. **Generated boilerplate.** What in the 17.8 KB blink TU is fixed per
   program (class-name table, poly dispatch, `sp_poly_cold` 5 KB pulled
   through `sp_class_name_fn`), and what codegen could emit only on use.

## Results

All on `tools/ch32-size.sh`, spinel tree `~/spinel-size-floor` branch
`mcu-trim`, `small` preset, `--no-bigint`, runtime and app at `-Os`,
getenv folded (`baremetal/noenv.h`), `SP_NO_GC_PHASES`. Bytes; flash =
text+data, RAM = data+bss (static only: stack and GC heap come out of the
rest of the 20 KB).

| lever | empty.rb flash | blink.rb flash | blink.rb RAM |
|---|---:|---:|---:|
| start (`docs/ch32v203-size.md`) | 47 244 | 64 240 | 14 632 |
| 1a `-msave-restore` | 45 372 | 61 912 | 14 632 |
| 1b LTO (on top of 1a) | 53 392 | 64 400 | 12 584 |
| 2 tiny stdio + no file system (on 1a) | 40 352 | 56 896 | 14 080 |
| 1b LTO again (on 2) | 48 256 | 59 328 | 12 008 |
| 4a `SP_NO_SLAB` (on 2) | 27 452 | 43 840 | 11 276 |
| pin move to upstream `0e124f742` (same flags) | 34 268 | 48 376 | 12 692 |
| packed builtin class table, id caches sized to the ids | **30 068** | **44 180** | **12 116** |

### Pin move to `0e124f742` (2026-11-14)

Same flags, `mcu-trim` rebased: empty grew 6.8 KB, blink 4.5 KB, RAM
1.4 KB, all in `sp_exc`. Upstream `ff6d831c2` and `4219e18e7` gave
exceptions a builtin class id and moved the class table by name into
`sp_exc.c`: the table stored each name in a fixed 48-byte row (122 rows,
6.3 KB of rodata for 1.6 KB of names), and three id-indexed caches came
with it (1.4 KB of RAM, over half of it for ids no class has). Blink
partly offset the flash growth: `sp_poly_cold` fell from 5.0 KB to 1.6 KB.

`mcu-trim` `0e4218b13` packs the names into one struct of exact-size char
arrays with a 4-byte `{offset, id}` row (still no load-time relocation),
and indexes the caches by `-id - 100` over the 96 ids in use. 4.2 KB of
flash and 576 B of RAM; 157 exception and class tests pass on the host.
Not MCU-specific, so it is a candidate upstream PR.

### 0. mruby/c baseline (measured)

mruby/c at `5bb8ae6`, all of `src/*.c` with its generated tables, linked
bare-metal with the same toolchain, flags (`-Os -msave-restore`,
gc-sections) and newlib-nano, driven by `baremetal/mrubyc/main.c` (no
scheduler, one C method, a 512-byte bytecode buffer, an 8 KB memory pool):

| build | flash | static RAM | of which the pool |
|---|---:|---:|---:|
| default (`MRBC_USE_FLOAT=2`: doubles) | 93 296 | 11 688 | 8 192 |
| `MRBC_USE_FLOAT=0` | 72 544 | 11 636 | 8 192 |

Largest pieces (no float): VM 14.5 KB, allocator 8.9 KB, Array 5.6 KB,
String 5.5 KB, symbols 4.9 KB, the Ruby-written builtins (`mrblib`) 4.9 KB,
Object 4.8 KB, the scheduler 4.2 KB, console 3.8 KB, class 3.3 KB, Hash
2.8 KB, task queue 1.7 KB.

What this says:

- mruby/c's footprint is fixed: it links every builtin class and method,
  because the bytecode can call any of them. The 40 KB on its README is
  RAM. Its flash is ~72 KB before trimming classes out by hand.
- spinel links only what the program uses: blink.rb is 42.8 KB with
  `SP_NO_SLAB`, and grows with the program, where mruby/c grows by bytes
  of bytecode.
- mruby/c's own static RAM is about 3.6 KB (the pool is the heap, sized by
  the application). spinel's is 11 KB before its heap: that is lever 3.

### 1. Compiler flags

- `-msave-restore`: kept, about 2.3 KB (shared prologue and epilogue
  routines; slightly slower calls).
- LTO: not kept for now. It saves about 2 KB of RAM (merged and removed
  statics), but costs 2–3 KB of flash: whole-program inlining puts a
  double-precision helper into `sp_gc_retune_object` that the non-LTO link
  drops. Worth a second look once the collector is simpler.

### 2. Tiny stdio (`baremetal/tinystdio.c`)

An integer-only `printf` family (%d %u %x %p %c %s, widths, l/ll/z) writing
to a weak `board_putchar`, the console `fputs`/`fwrite`/`putchar`, `exit`
with its own atexit table, and failing stubs for the file API (`fopen`
answers ENOSYS, `sscanf` matches nothing: Time-string parsing raises). The
runtime references the file API from code that is dead in an MCU program,
but resolution would still pull newlib's stdio objects, so the stubs keep
them out. Linked with `-z muldefs` so the first definition (ours) wins.
About 5 KB of flash and 0.5 KB of RAM. After it, newlib contributes only
malloc, string functions and qsort.

### 4a. `SP_NO_SLAB` (spinel `mcu-trim`)

The slab needs a 4 MB-aligned reservation, so on ESP32-class and smaller
parts it already switches itself off at run time and every block is a
malloc. The switch says so at compile time: `sp_slab_on` and
`sp_slab_owns()` are constant 0 outside `sp_slab.c`, the entry points the
collector calls unconditionally become inline stubs with their slab-off
behaviour, and the allocation front skips its slab fast path. About 13 KB
of flash and 2.5 KB of RAM.

**Verified on the host GC torture corpus** (2026-11-13):
`tools/preset-stress.sh small` with
`EXTRA_DEFS="-DSP_NO_SLAB -DSP_NO_GC_PHASES"`, the `mcu-trim` tree (which
also carries the fixed-point minor cadence), compiler and base archive from
the vendored build at the same commit: **721 pass, 1 fail, 61 skip** (783
runs). The one failure, `gc_ivar_set_receiver_root` in gc-minor mode, is an
upstream bug, not the switch: it fails the same way on the untouched
vendored tree with nothing but `SPINEL_GC_SLAB=0`, and passes with the slab
on. Cause: with the slab off, allocations come from per-class header pools,
and `sp_gc_pool_relink` kept a dead object's `frozen` bit, so the next
object of a class that reused a frozen object's header came out frozen.
That affects every board running spinel today (the slab is off on ESP32
too). Fix on spinel branch `fix-pool-frozen` (`39c155f8a`, clears `frozen`
and `aged` on relink), cherry-picked into `mcu-trim`; with it the test
passes in all three modes, and a targeted re-run (pools, finalizers, frozen
objects, ObjectSpace, fibers, exceptions, Struct, Proc: 126 runs) is 111
pass, 0 fail, 15 skip.

### What is left in blink.rb (42.8 KB)

Generated app TU 16.1 KB, `sp_exc` 10.9 KB, `sp_gc` 7.3 KB, `sp_poly_cold`
5.0 KB, `sp_cold` 1.6 KB, tiny stdio 1.2 KB, `sp_io` 1.1 KB, `qsort` 0.9 KB
(the mark), 64-bit divide 1.6 KB (`sp_gc_collect`, the formatter's 64-bit
path).
