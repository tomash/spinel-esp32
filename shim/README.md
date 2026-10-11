# shim/esp32/ -- FreeRTOS/newlib compatibility layer for libspinel_rt

Mirrors vendor/spinel/lib/wasi/ (the in-tree precedent for porting the runtime
to a constrained target): header shims + one .c of POSIX-surface stubs, so that
every runtime object *compiles*, and only a program that actually reaches for
an unsupported feature pays at link/run time (gc-sections drops the rest).

## Why per-header (not one big stub file)

The runtime `#include`s its POSIX surface from many TUs. newlib (ESP-IDF)
provides: string/stdlib/stdio/stdint/unistd/errno/math/limits/time/stdarg/
fcntl/ctype/sys/types.h + more. Missing or partial: signal.h, poll.h,
sys/mman.h, sys/socket.h, spawn.h, sys/resource.h, sys/wait.h, pthread.h
(single-threaded rt only needs no-op locking, already compiled out), and
ucontext.h (only under the non-ASM fiber path — see src/sp_ctx_riscv.c).

## Status

- [ ] include/ headers (compile-surface for missing POSIX headers)
- [ ] src/sp_esp32.c: mmap -> static buffers, signal stubs, fork/popen ENOSYS
      (modeled on lib/wasi/sp_wasi.c)
- [ ] src/sp_ctx_riscv.c: sp_ctx_swap/sp_ctx_make for RV32IMAC (C3),
      gated like lib/sp_fiber_ctx.h's SP_FIBER_ASM
- [ ] src/app_main_adapter.c: FreeRTOS task trampoline that calls the spinel
      main body on a pinned static stack (fixes findings.md B1+B5)
- [ ] src/alloc_caps.c: optional malloc policy for MALLOC_CAP_INTERNAL vs
      SPIRAM (heap_caps-backed malloc, chosen via sdkconfig/Kconfig)

Patches carried against vendor/spinel itself live in ../../patches/ and are
applied by tools/apply-patches.sh (kept separate from this shim, which is
new code, not modifications of upstream files).
