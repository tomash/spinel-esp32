# Xtensa Port Notes — Session Log (2026-10-12)

Working notes from the esp32 (Xtensa LX6) port session. The plan itself
(`plan-xtensa-port.md`) lives in the main worktree (untracked, owned by the
planning session); this file records what that plan got right, what it got
wrong, and everything the next session needs to know.

## Worktree / environment

- Branch `xtensa-port` in worktree `~/spinel-esp32-xtensa`, cut from
  c931edc ("M5: mDNS + NTP wall clock + GC tuning + socket-error resilience").
  The main worktree has the M6/OTA agent's uncommitted work — do not touch it.
- vendor/spinel here is a plain shared clone pinned to 6b6f4602c with patches
  0030/0040 applied via `tools/apply-patches.sh` (stamp:
  `.spinel-esp32-patched`). The `spinel` host binary is a gitignored build
  artifact; if missing, copy it from `~/spinel-esp32/vendor/spinel/spinel`.
- Xtensa toolchain:
  `~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/`
  (installed via `idf_tools.py --non-interactive install xtensa-esp-elf`).
  riscv32 toolchain was already present.
- Pitfall: two toolchains coexist under `~/.espressif/tools` — `build-rt.sh`
  discovers exact gcc/ar names per target, don't regress it to a loose find.
- This xtensa gcc has no `-mcpu=`/`-march=` flags; defaults already emit
  windowed ABI (`entry a1,48`, `call8`, args a2–a7).

## Stage 2 verdict: ucontext asm is NOT needed for parity (deferred)

- fmruby-core (family-mruby), our newer-spinel reference
  (spinel @ 622750cf, branch fmrb-dev), never solved the Xtensa fiber switch:
  their `sp_fiber_ctx.h` adds an `SP_NO_MMAN` build branch that drops
  `sp_fiber.c`/`sp_sched.c` from the RT entirely on RTOS targets. Plan's
  "Option B: copy their asm" is a dead end — no such asm exists upstream.
- Verified no parity app uses `Fiber`/`Thread` (grep over `app/*.rb`) and the
  esp32 RT archive contains no ucontext symbols.
- `build-rt.sh esp32` therefore skips `ucontext_xtensa.S` with a warning;
  **but the archive is NOT linkable as-is** — see the fiber pull-chain
  discovery below. Final state: the shim carries ENOSYS link-time plugs.
- If Fiber on Xtensa is ever required, model it on newlib xtensa setjmp
  (studied: `entry a1,16; movi a2,0; syscall` window flush, save-area copies
  at sp−16/sp, a0-window-bits at env+64, longjmp invalidates windows via
  `wsr WINDOWSTART`, retw underflow refill). NOT a flat RV32 register copy.

## Fiber pull-chain (corrects "archive linkable without asm")

sp_fiber.o/sp_sched.o are ALWAYS pulled from the RT archive, on every
target, in our pin (6b6f4602) — the cooperative M:N scheduler is compiled
in even when no Fiber/Thread is used by the app:

- sp_io.c:220/255 `sp_sched_wait_io_unless` is under `#ifdef SP_THREADS`,
  but the `#else` branches call `extern void sp_Thread_pass(void)`
  unconditionally (sp_io.c:226/264, sp_cold.c:2323/2335).
- sp_io.c:550/578/586 `sp_sched_ev_forget` (no-op definition in sp_sched.c),
  sp_system.c:72/sp_cold.c:1438 `sp_sched_wait_child` — unconditional.
- spinel_rt.h:1525 GC globals-marking calls `sp_mark_fiber_root_storage()`
  unconditionally (defined in sp_fiber.c). fmruby's tree guards this AT THE
  CALL SITE under SP_NO_MMAN; our pin does not.
- spinel_rt.h poly dispatch (Fiber/Thread/Enumerator paths, lines ~9236,
  13764–13827, 18049–18086, 19695) reference sp_Thread_* and
  sp_Fiber_* by name unconditionally.

On Xtensa, `sp_fiber_ctx.h` takes the POSIX ucontext fallback
(SP_FIBER_ASM=0) and `ucontext_xtensa.S` does not exist, so
getcontext/makecontext/swapcontext/setcontext were undefined at link.
Fix (shim/esp32/src/sp_esp32_stub.c, `#ifdef __xtensa__`): ENOSYS/abort
link-time plugs. These are UNREACHABLE, not a degraded path — Fiber.new
mmaps its stack, the shim mmap answers MAP_FAILED, sp_fiber.c raises
FiberError before any context exists; resume/yield/Enumerator check
f->stack first and raise the same way. Guarded so a future real asm (or
accidental RV32 compile) is a duplicate-definition error, not silent
shadowing. Verified no ucontext undefineds remain in the final link.

## RT build status

- `build/rt-esp32/libspinel_rt_esp32.a` — 4175114 bytes, ELF 32-bit LSB
  Xtensa (little-endian; the generic-driver big-endian fiasco below is
  fixed). Regression suite passes for riscv32 (3924398 bytes).
- `sp_int` is `intptr_t` in `sp_types.h` — 4 bytes on both targets, so the
  generated-C type inference is target-independent. Confirmed against the
  emitted `blink_tdisplay.c`.

## Stage 3/4 result (esp32 build + T-Display flash) — DONE

- First esp32 firmware link failed with
  `dangerous relocation: windowed longcall crosses 1GB boundary: *UND*`
  in sp_poly_eq_deep — root cause was a STALE firmware/main/app.c (the
  committed M5 status-server app, pulling a huge RT surface), NOT the
  longcall machinery itself. Regenerating app.c from app/blink_tdisplay.rb
  made it link. **Gotcha: regenerate firmware/main/app.c per app before
  debugging exotic link errors.**
- `tools/build-app.sh app/blink_tdisplay.rb --target=esp32 --flash`
  end-to-end: emit C (int-size probe via xtensa-esp32-elf-gcc) → idf.py
  set-target esp32 → build → flash. Image 471968 bytes, 55% of the 1MB
  factory partition free. Newlib "not implemented" warnings for
  chdir/chmod/dirfd/_execve/_fork/getcwd/_wait are benign (gc-sections note).
- Flash on /dev/ttyUSB1 + serial capture (tools/monitor.py, 14s): bootloader
  clean, `spinel: spinel app_main: calling generated main()`, blink prints,
  `main() returned 0` after ~4.4s. GPIO4 backlight toggling from Ruby works
  on hardware — first Xtensa spinel execution.
- idf-env gotcha: `idf_tools.py install --targets esp32,esp32c3` needed
  once, otherwise export.sh exports no xtensa tools.
- Classic esp32 = line-fitting ADC calibration only (no curve-fitting) —
  adc_cali guards in firmware CMake are target-conditional.

## Stage 5 checklist (T-Display specifics) — verify before touching

- **Battery ADC pin**: plan says GPIO36, many LilyGO T-Display docs say
  GPIO34. Must verify against LilyGO docs/schematic before `battery.rb`.
- **No user LED on T-Display**: GPIO4 is the backlight enable; `blink_tdisplay.rb`
  blinks the display backlight instead (screen flashes, no LED).
- **mDNS hostname collision**: both boards advertising the same hostname will
  collide on Stage 5. Parametrize per board (e.g. `spinel-esp32-tdisplay`
  vs `spinel-esp32-c3`) — decide whether from `SPINEL_ESP_PORT` env or
  IDF target.
- **GC retune**: esp32-class chips have more DRAM than the C3; retune
  `SP_GC_STACK_MAX`/heap constants using `esp32_heap_free` telemetry once
  wifi_status.rb runs on hardware. Keep RT and generated-C constants in
  lockstep (silent ABI break otherwise — fmruby's documented pitfall).
- ST7789 display stays out of scope (plan parenthetical calling it I2C is
  wrong — it's SPI — but irrelevant while display is out of scope).
- OTA partition work belongs to the M6 agent's branch, not this one.

## Board / hardware facts

- C3 = T-OI Plus, /dev/ttyUSB0 (CH34x), LED GPIO3. This is the default target.
- esp32 = T-Display, /dev/ttyUSB1 (CP2104, serial 02163285 — verify via
  dmesg before flashing; ttyUSB order is unstable, always pass `--port`).
- Serial monitor output needs `grep -a` (binary bytes in stream).
- Multi-line git commit messages must be written via a `/tmp/*.sh` script
  (PTY tool limitation).

## Session TODO

- [x] Stage 0 toolchain
- [x] Stage 1 build-rt.sh parametrization (commit cdd7fcb)
- [x] Stage 3 IDF config + build-app.sh targets + blink_tdisplay.rb
      (commit b3da517)
- [x] Findings doc (this file) committed
- [x] Stage 4 flash + monitor on real T-Display (done 2026-10-12: blink OK)
- [ ] Stage 5 app bring-up sequence (hello → battery → wifi_status → GC retune)
