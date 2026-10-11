# Spinel runtime: what an ESP32 port must change (findings)

Research date: 2026-10-11. All file references against matz/spinel master @ clone time.
Method: read the source; built spinel on this machine and compiled/ran programs to verify claims.

## Positives (why this port is viable at all)

- **Whole-program AOT → C → native binary.** No VM on target. `puts "hi"` emits a
  35-line C file + `lib/libspinel_rt.a` (~3.0 MB archive on x86-64) + `-lm`.
- **32-bit targets are anticipated**: `lib/sp_types.h` — `sp_int` is `intptr_t`,
  with an explicit comment naming "32-bit embedded targets"; `#error` only if
  intptr_t is neither 32 nor 64 bits. Integer inference retargets from the C
  compiler's `__SIZEOF_POINTER__` probe.
- **`--cc=CMD`**: spinel asks the named compiler its pointer width and infers
  32-bit integers for a 32-bit cross-gcc (src/main.c ~line 893). No spinel
  rebuild needed for word-size retargeting.
- **Precedent for constrained targets in-tree**: `--target=wasm32-wasi` exists,
  implemented as `lib/wasi/` — a directory of header shims (netdb.h, pwd.h,
  signal.h, ucontext.h, unistd.h, sys/) over a limited libc, plus a special
  runtime variant (`wasm32-wasi/libspinel_rt_noexc.a`, `--exceptions=abort`).
  This is the structural template for `lib/esp32/`.
- **Section GC works**: linking with `-ffunction-sections -Wl,--gc-sections`
  drops unused runtime objects (sp_net.o, sp_process.o pull poll.h/spawn.h/
  sys/socket.h — none of which newlib has — but they vanish if unreferenced).
  Host hello binary: ~172 KB dynamic, ~2.7 MB static(glibc); ~100 text symbols
  reached from main.
- **Non-MT runtime is pthread-free by default**: `lib/sp_alloc.h` compiles heap
  locking to no-ops in the single-threaded runtime (libspinel_rt.a);
  -lpthread only for libspinel_rt_mt.a.
- **FFI**: `ffi_func`/`ffi_lib`/`ffi_const`/`ffi_buffer` etc. emit externs,
  no extension compiler on the build path. This is the binding layer for
  ESP-IDF APIs. Packages can carry raw `.c` files.
- **GC is mark/sweep**, roots supplied by the generated TU via hooks
  (`sp_gc_mark_globals_hook` / `sp_gc_str_sweep_hook`). No arena tied to
  mmap; allocations go through malloc → ESP-IDF heap.

## Blockers found (must-fix list)

### B1. Every program runs on an mmap'd coroutine stack
`sp_main_stack_run` (lib/sp_fiber.c:489) mmaps a guarded stack
(MAP_PRIVATE|ANONYMOUS|MAP_STACK|MAP_NORESERVE), mprotects a guard page,
then switches onto it — even for hello world. newlib/FreeRTOS has no mmap.

**Fix**: static aligned buffer (e.g. in .bss or PSRAM-backed), no guard page;
register the bounds so the fault-handler machinery can be told "none".
The code path wants known stack bounds; a static stack gives them trivially.

### B2. No context-switch asm for xtensa/riscv32
`SP_FIBER_ASM` covers x86_64 and aarch64 only (lib/sp_fiber_ctx.h:30);
everything else falls back to `swapcontext` — which ESP newlib does not
implement. Needed: `sp_ctx_swap` + `sp_ctx_make` for the target ISA.
~40 lines of asm; two in-file examples to mirror (x86_64, aarch64).

**Fix**: RISC-V (C3): callee-saved regs = s0–s11 (+ sp). ESP-IDF's own
context switch code is a crib for both ISAs. Decide M1 whether Fibers are
in scope for M2 or if we take the `--exceptions=abort`-style degraded mode
first: a no-fiber runtime where the main body runs on the task stack and
Fiber.new is refused.

### B3. Signal machinery
Stack-guard fault handler (SIGSEGV via sigaction) + SIGUSR-based memory
reporter (lib/sp_alloc.c:1419–1530). FreeRTOS/newlib has no signals.

**Fix**: stub `sigaction`/handler arming in the shim; no MMU → guard pages
are meaningless anyway. Disable `SP_*` watchdog paths.

### B4. POSIX header surface
Runtime includes (counts from lib/*.c): string.h(49) stdlib.h(38) stdio.h(25)
stdint.h(25) unistd.h(15) errno.h(14) math.h(12) limits.h(10) time.h(9)
stdarg.h(9) signal.h(9) fcntl.h(9) ctype.h(8) sys/wait.h(8) pthread.h(7)
sys/stat.h(6) sys/types.h(6) poll.h(5) sys/resource.h(4) sys/mman.h(4)
malloc.h(4) sys/socket.h(3) spawn.h(3)…

newlib has: string/stdlib/stdio/stdint/unistd/errno/math/limits/time/
stdarg/fcntl/ctype/sys-types. Missing: signal.h, poll.h, sys/mman.h,
sys/socket.h, spawn.h, sys/resource.h, sys/wait.h, pthread.h (as used).

**Fix**: mirror `lib/wasi/` — shim headers for the missing ones, providing
ENOENT declarations/structs so unused objects compile; rely on --gc-sections.

### B5. `main` shape / startup
Generated programs are `int main(argc, argv)` calling `sp_main_stack_run`
and expect `exit()` handlers (`sp_fin_run_exit`). On ESP32 the entry point
is `app_main(void)` in a FreeRTOS task.

**Fix**: tiny adapter — `app_main` creates a FreeRTOS task with a pinned
static stack that calls the spinel main-body shim. ENV/ARGV are empty.

### B6. stdout
`puts` → stdio → newlib → UART via ESP-IDF syscalls (or console retarget).
ESP-IDF gives `STDOUT_LINE_BUFFERED`/VFS; mostly free.

## Non-blockers worth noting

- `setjmp/longjmp` (exceptions) exists in newlib — begin/rescue should work
  unmodified once B1–B3 are handled.
- `sp_int` = 32-bit on target: Integer arithmetic narrows at INT32 bounds
  (documented, by-design on 32-bit builds).
- String literals are static structs in the generated TU — flash-resident
  (.rodata), no copy at startup.
- GC trims/malloc_trim is already a no-op off glibc.
- seeded Random differs from CRuby (PCG vs Mersenne) — same as on Linux,
  not an ESP32 issue.

## Sizing estimate (to validate on hardware in M2)

- App flash: generated C (small) + runtime sections that survive
  --gc-sections. Expect O(100–300 KB) @ -Os. 
- RAM: GC heap = malloc'd on demand; string slabs. Budget the FreeRTOS
  heap caps explicitly (MALLOC_CAP_INTERNAL vs SPIRAM) in the allocator shim
  if needed.
- One FreeRTOS task owns all Ruby state (GC, fibers). Other tasks must not
  touch Ruby values — same discipline as picoruby.

## Toolchain notes (host machine, 2026-10-11)

- gcc ✓, make ✓, cmake 3.28 ✓, ruby 3.3.6 ✓, python 3.12 ✓ (host-side only).
- NO ESP toolchain installed: no riscv32-esp-elf-gcc, no xtensa-esp-elf-gcc,
  no idf.py, no esptool. No ~/esp, no /opt/esp-idf.
- Decision point for M1: install ESP-IDF (brings idf.py/esptool — Python,
  host-side) or go bare toolchain (espup/rust-free path: download Espressif
  toolchain tarballs, hand-rolled linker scripts, esptool via pip).
  "No Python on target" is honored either way; host-side Python is an
  installer convenience, not a runtime dependency.
