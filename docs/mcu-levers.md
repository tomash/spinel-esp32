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
   measured number, not a README one. Blocked for now: building it runs its
   table generators (Ruby scripts from the cloned repo), which needs the
   user's go-ahead.
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
| 4a `SP_NO_SLAB` (on 2) | **27 452** | **43 840** | **11 276** |

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
of flash and 2.5 KB of RAM. Not yet verified on the host GC torture corpus
(`tools/preset-stress.sh` with `-DSP_NO_SLAB`), which has to pass before
it goes upstream.

### What is left in blink.rb (42.8 KB)

Generated app TU 16.1 KB, `sp_exc` 10.9 KB, `sp_gc` 7.3 KB, `sp_poly_cold`
5.0 KB, `sp_cold` 1.6 KB, tiny stdio 1.2 KB, `sp_io` 1.1 KB, `qsort` 0.9 KB
(the mark), 64-bit divide 1.6 KB (`sp_gc_collect`, the formatter's 64-bit
path).
