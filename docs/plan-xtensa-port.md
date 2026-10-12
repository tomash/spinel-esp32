# Xtensa Port (classic ESP32, LilyGO T-Display) — Implementation Plan

> **For the implementing agent**: repo state at commit `c931edc` (M5) or later.
> Working dir `~/spinel-esp32`. **Never build in `~/clacky_workspace`** (pCloud FUSE).
> The board on `/dev/ttyUSB1` (CP2104, mode 0666, verified present) is a
> **LilyGO T-Display v1.1-classic: ESP32 Xtensa LX6 dual-core, 4 MB flash, no FPU used**.
> First board (T-OI Plus, ESP32-C3, `/dev/ttyUSB0`) stays the lead target — nothing in
> this plan may regress it.

## Goal

`IDF_TARGET=esp32` (Xtensa) build of the same firmware: same `app.c`, same Ruby apps,
same shim, running on the T-Display — M0-blink parity, then ride up to M5 feature
parity (WiFi status server, mDNS, NTP) almost for free.

## Why it's mostly done already

Spinel emits C; `app.c`, the FFI externs, `sp_app_adapter.c`, `sp_lwip_compat.c`,
`sp_esp32_stub.c`, patches 0030/0040, and all Ruby code are arch-neutral. The real
arch surface is exactly three things:

1. `tools/build-rt.sh` — toolchain discovery + `-march=rv32imc_zicsr_zifencei -mabi=ilp32` hardcoded
   (lines 28, 38-39, 90, 98).
2. `shim/esp32/src/ucontext.S` — RV32IMAC-only coroutine switch (~90 lines of asm).
   This is **the whole project risk**. Everything else is make/plumbing.
3. IDF project config: `IDF_TARGET`, sdkconfig, flash/partition params, dual-core.

## Reference: fmruby-core already solved this

`github.com/family-mruby/fmruby-core` (GPL-3.0) runs **Spinel-emitted C on ESP32-S3 Xtensa**
with a vendored compiler pinned via `components/fmrb_spinel_rt/SPINEL_PIN`. Before
writing any Xtensa asm, **read their `components/fmrb_spinel_rt/`** — their ucontext /
fiber / RT solution may replace the risky step below outright (license: GPL-3.0 — fine
to *read and be inspired by*; if you copy code verbatim into our repo, note the license
implication for a future public release).

---

## Stage 0 — Toolchain (~30 min, ~1 GB download)

```bash
cd ~/esp/esp-idf-v5.5
python3 install-env.json 2>/dev/null; # or the canonical form:
python3 tools/idf_tools.py --non-interactive install xtensa-esp-elf
```

- Currently only `riscv32-esp-elf` exists under `~/.espressif/tools/`.
- Verify: `find ~/.espressif -name xtensa-esp-elf-gcc -maxdepth 6 -type f` → executable,
  and matching `xtensa-esp-elf-ar` next to it.
- Do **not** disturb the riscv32 install — C3 builds must keep working.

## Stage 1 — Parametrize `tools/build-rt.sh` (< 1 h)

Add a target parameter; keep riscv32 as the default so existing commands are unchanged:

```bash
tools/build-rt.sh                 # riscv32 (esp32c3), as today
tools/build-rt.sh esp32           # xtensa
```

Changes:
- Toolchain discovery loop (line 28): search `<target>-esp-elf-gcc` instead of hardcoded
  `riscv32-esp-elf-gcc`.
- `CC`/`AR` prefixes derived from the target (lines 38-39).
- Xtensa flags: drop `-march`/`-mabi` entirely (defaults are correct for ESP32:
  windowed ABI, no FPU needed since spinel is soft-float everywhere). No
  `rv32imc_zicsr_zifencei` equivalent needed.
- Output dir `build/rt-riscv32` → `build/rt-$target` (line 90).
- Update `--probe` mode to accept the target too.
- Test: `tools/build-rt.sh esp32` produces a clean `libspinel_rt.a` with
  `xtensa-esp-elf-ar` members — **except** `ucontext.o` which needs Stage 2 first;
  either do Stage 2 asm first or temporarily exclude it.

## Stage 2 — `ucontext.S` Xtensa rewrite (4–8 h, the real work)  ⚠️

### Problem

The RV32 version saves/restores callee-saved regs (`ra, sp, s0..s11`) as a flat
copy — valid because RV32 has no register windows. Xtensa's **windowed register ABI**
breakks this model: `s0..s11` don't exist as stable slots; registers live in moving
windows (`a0..a15` per window of 4/8), and underflows/overflows spill them to the
stack automatically via `window underflow/overflow` exceptions handled by the
compiler's window-handling prologue.

### Approach

Cooperative-only coroutine switch (no preemption, no signals), no FPU, no
interrupt-handler switching. The standard portable trick for Xtensa:

**Option A (recommended, simplest correct):** switch via `setjmp`/`longjmp`
semantics — newlib *does* ship `setjmp` for xtensa (`machine/setjmp.h`) which already
handles window saves correctly via `__xt_mp` / the compiler's `setjmp` implementation.
Reimplement `getcontext/setcontext/swapcontext/makecontext` on top of the same
save/restore pattern the Xtensa `setjmp` uses:

- `getcontext`: `setjmp`-style save of `sp` (a1), `ra` (return PC), and the current
  window's live callee-saved regs (in windowed ABI: a12-a15 — but these are already
  spilled to the *old stack* by the compiler's window handling, so a setjmp-style
  capture of `sp`+`return-PC`+`loop` regs suffices for a cooperative switch).
- `setcontext`: restore `sp`, set `ra` to saved PC, `ret`.
- `makecontext`: prime new stack top with `func` and `uc_link`, set saved `sp` and
  saved `ra = __start_context` trampoline, same as current implementation's logic
  (top-16 layout, see `include/ucontext.h` lines 20-27) — adapt to Xtensa's
  16-byte-aligned stack requirement (`entry` / window ABI).
- **Window size caveat**: no `call8/call12` (window 8/12) forms — stay in the base
  window (window size 4, `call4`-equivalent calling convention) for the trampoline so
  no spill/fill is triggered in the switch functions themselves. Mark the switch
  functions `.literal_position`-clean and no-window (`--no-windowed-frame` not a
  real flag — instead just write them in the minimal asm style with no
  `call` instructions inside).

**Option B:** copy fmruby-core's approach verbatim (GPL-3.0) after reading
`components/fmrb_spinel_rt/` — likely their asm already implements Option A or a
window-spill-aware variant.

### Struct layout

Keep the existing `ucontext_t` layout (`include/ucontext.h`) byte-identical for
Xtensa — same offsets (+0 uc_link, +4 ss_sp, +8 ss_flags, +12 ss_size, +16 `__regs[14]`)
— just repurpose `__regs[]` slots: `[0] pc/ra`, `[1] sp`, `[2..]` unused (or used for
`loop` regs if needed). Keeping layout identical lets both `.S` variants coexist
compiled from the same header.

### File organization

```
shim/esp32/src/ucontext.S        # stays RV32 (unchanged)
shim/esp32/src/ucontext_xtensa.S # NEW
shim/esp32/include/ucontext.h    # unchanged (shared layout)
```

`build-rt.sh esp32` compiles `ucontext_xtensa.S` instead of `ucontext.S` into the archive.

### Testing path

1. **Host-side first**: compile `ucontext_xtensa.S` with `xtensa-esp-elf-gcc -c` —
   zero build errors.
2. **Emulated**: QEMU `qemu-system-xtensa` if available locally, or skip straight to
   hardware given cooperative-only complexity.
3. **On-hardware smoke test** (write `app/fiber_test.rb` — two fibers ping-pong
   1000 iterations, print counter) — this is where the 4-8 h estimate is spent.
   Classic failure modes: works 99 times, dies on the 100th (window misalignment);
   crashes only when a fiber's stack crosses a page; works with -O0 not -O2.
   Mitigation: test both -O0 and -O2, and with `CONFIG_ESP32_DEBUG_OCDA_BREAK` off.

## Stage 3 — IDF project for ESP32 (~1-2 h)

- Copy/configure `firmware/` build for `IDF_TARGET=esp32` — either a second
  `sdkconfig.defaults.esp32` + `idf.py -D SDKCONFIG_DEFAULTS=...`, or a separate
  `firmware-esp32/` dir if the CMake doesn't parametrize cleanly.
- Key sdkconfig deltas from the C3 config:
  - `IDF_TARGET=esp32`
  - **Dual-core**: keep D3 (single-task Ruby) — pin the Ruby task to **PRO_CPU (core 0)**
    (`CONFIG_ESP_TASK_MAIN_CORE` / `xTaskCreatePinnedToCore` in `sp_app_adapter.c` —
    check how the main task is spawned there; if it currently uses plain
    `xTaskCreate`, pin it explicitly on esp32).
  - Main task stack: the C3 value (49152) came from M4 DRAM tuning; ESP32 classic has
    ~100+ KB more DRAM headroom → keep 49152, or revisit after first boot.
  - Flash: 4 MB → **same `partitions.csv` works** if M6 landed first (ota_0/ota_1);
    if not, factory layout unchanged is fine for M0-parity (flash limits: 4 MB).
- `CONFIG_FREERTOS_UNICORE` must **stay off** (it's a C3-only setting) — no change needed,
  just verify.
- ESP32 classic needs no PSRAM handling (T-Display has none) — no change.

## Stage 4 — Board bring-up: T-Display (~1 h to blink parity)

- **No user LED** on the classic T-Display — GPIO4 is the **backlight enable** pin;
  blink that, or GPIO22/27 (the two exposed-through-connector GPIOs on the T-Display's
  left header) with a jumper LED.
- `app/blink.rb`: add a `board: :tdisplay` branch or parametrize GPIO at the top —
  simplest: new `app/blink_tdisplay.rb` copied from blink.rb with GPIO4/backlight.
- `sp_app_adapter.c`/FFI: `esp32_led_*` bindings are just GPIO wrappers — same FFI,
  different pin number. No new binding needed unless we want PWM for the backlight —
  plain on/off blinks fine through the backlight enable pin.
- Flash: `idf.py -D IDF_TARGET=esp32 -D PORT=/dev/ttyUSB1 flash monitor` —
  or just `esptool`/`idf.py --port /dev/ttyUSB1`. Verify dmesg serial `02163285`
  matches the board in use before flashing.

## Stage 5 — Ride up to M5 parity (near-zero additional work)

Once blink works, in order — each should be a rebuild + reflash + smoke test only:

1. `hello.rb` → UART stdout (verify `puts` over the CP2104)
2. `battery.rb` → ADC: **T-Display's battery ADC pin is GPIO36 (ADC1_CH0)**, not GPIO2
   (C3). Divider differs (T-Display uses 2:1 too — verify against LilyGO's
   `T-display` battery docs) — this is a 2-line change in `battery.rb`.
3. `wifi_status.rb` → WiFi: same SSID/PSK constants, same FFI, same lwip compat —
   expect it to just work. mDNS name will **collide** with the C3 board on the same
   LAN if both run simultaneously — parametrize the hostname (`spinel-esp32-tdisplay`
   or `spinel-esp32-c3`) via an env/config constant per build.
4. GC tuning: reuse `esp32_gc_tune` but **retune thresholds for the ESP32's larger
   DRAM** — start from the C3 values (sp_gc_threshold 8KB, sp_str_threshold 4KB,
   sp_str_old_threshold 8KB) and raise `sp_str_threshold` if soak shows headroom.
   Classic ESP32 has ~100+ KB more free DRAM than C3 → should be more forgiving,
   but don't assume — run the same M5-style burst soak and watch
   `esp32_heap_free` telemetry.

## Sequencing with M6 (OTA)

If M6 hasn't landed yet, do this port first or land M6 first — either works, but
**the partition-table change in M6 (ota_0/ota_1/otadata) is written once and
shared by both targets**, since both boards are 4 MB flash. If M6 is done, this
port inherits OTA for free (same `esp_ota_*` calls are target-agnostic).

## Effort estimate (total: ~2–4 focused sessions)

| Stage | Work | Est |
|---|---|---|
| 0 | Toolchain install | 30 min |
| 1 | build-rt.sh parametrize | < 1 h |
| 2 | ucontext.S → Xtensa | **4–8 h** (incl on-hw debugging) |
| 3 | IDF project config esp32 | 1–2 h |
| 4 | T-Display blink | 1 h |
| 5 | Up to M5 parity (WiFi/mDNS/NTP/GC) | 1–2 h |

## Pitfalls (project-specific, do not relearn)

- **Never** build in `~/clacky_workspace`.
- `xtensa-esp-elf-gcc` and `riscv32-esp-elf-gcc` both live under `~/.espressif/tools/`
  — the build-rt.sh discovery `find -maxdepth 6` must not pick up the wrong one;
  parametrize by exact prefix, not by loose find.
- CP2104 on `/dev/ttyUSB1` (T-Display) vs CH34x/C3 on `/dev/ttyUSB0` — always pass
  `--port` explicitly; ttyUSB order is not guaranteed across reboots.
- GPIO2/ADC mapping differs per board — **check LilyGO T-Display docs, not the C3 docs**
  (backlight=GPIO4, battery ADC=GPIO36, I2C for the ST7789 is on GPIO21/22).
- ST7789 display driver is explicitly **out of scope** for this milestone (separate
  later work) — do not start it even though the board tempts you with its screen.
- Dual-core: keep D3 (single-task Ruby) — pin to core 0, don't get creative.
- Multi-line git commits via PTY will fail — write to `/tmp/*.sh` and bash it.
- Serial monitor noise: `grep -a`.
- `sdkconfig` is generated — edit `sdkconfig.defaults`, not `sdkconfig`, then reconfigure.