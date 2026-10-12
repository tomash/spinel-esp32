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
  an app referencing Fiber fails loudly at link time (honest failure mode).
- If Fiber on Xtensa is ever required, model it on newlib xtensa setjmp
  (studied: `entry a1,16; movi a2,0; syscall` window flush, save-area copies
  at sp−16/sp, a0-window-bits at env+64, longjmp invalidates windows via
  `wsr WINDOWSTART`, retw underflow refill). NOT a flat RV32 register copy.

## RT build status

- `build/rt-esp32/libspinel_rt_esp32.a` — 3698680 bytes, 38 members, ELF
  32-bit MSB Xtensa. Regression suite passes for riscv32 (3924398 bytes).
- `sp_int` is `intptr_t` in `sp_types.h` — 4 bytes on both targets, so the
  generated-C type inference is target-independent. Confirmed against the
  emitted `blink_tdisplay.c`.

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
- [ ] Stage 3 IDF config + build-app.sh targets + blink_tdisplay.rb (code
      done; pending: esp32 `idf.py build` green + commit)
- [ ] Findings doc (this file) committed
- [ ] Stage 4 flash + monitor on real T-Display (needs user/hardware)
- [ ] Stage 5 app bring-up sequence (hello → battery → wifi_status → GC retune)
