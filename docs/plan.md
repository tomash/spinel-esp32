# Milestones

## M0 — host pipeline, proven in-repo  [this commit]

- [x] New repo, docs, submodules pinned
- [x] Build spinel from vendor/spinel on this machine
- [x] Compile + run a Ruby app via the spinel we built (hello + fib demo)
- [x] Verify `-S` C generation, `--cc=` retargeting flags, static link path
- [x] Record runtime research in docs/findings.md
- Deliverable: anyone can clone this repo and reproduce the host pipeline.

## M1 — toolchain + cross-compile the runtime [blocked on install decision]

- [ ] Install ESP32 toolchain (decision needed: ESP-IDF vs bare Espressif
      toolchain; see "Toolchain notes" in findings.md)
- [ ] Cross-compile `libspinel_rt.a` for riscv32-esp-elf (C3 first) with
      shim/esp32 headers; stub list B1–B4; get a clean archive build
- [ ] Same for xtensa-esp-elf (classic ESP32/S3) — asm switch is harder
- [ ] Decide fiber scope: full asm switch vs no-fiber degraded mode first
- Deliverable: `libspinel_rt.a` for each ISA + a compile-only smoke test.

## M2 — "Hello UART" on hardware [blocked on hardware decision]

- [ ] app/hello.rb → app.c → link against ESP runtime in an ESP-IDF
      component (or bare linker script)
- [ ] app_main adapter (B5), static main stack (B1), stdout over UART (B6)
- [ ] Flash to a real board, see Ruby's puts over serial
- Deliverable: photo/photo-worthy first boot: `puts "Hello from Ruby"` @ 115200.

## M3 — hardware bindings + real firmware

- [ ] FFI surface for ESP-IDF: gpio, delay, i2c/spi, uart (ffi_const/ffi_func)
- [ ] GC budget checks under load; heap-cap policy
- [ ] Example apps: blink.rb, i2c sensor read, Wi-Fi scan (needs network shim)
- Deliverable: a demo app that is *obviously* easier to write in Ruby than C.

## M4 — upstream

- [ ] Rebase patches onto spinel master, open `--target=esp32` PR mirroring
      lib/wasi/ (they already accepted one constrained target in-tree)
- Deliverable: PR to matz/spinel + a RubyKaigi-grade writeup.

## Standing decisions

- D1: RISC-V (ESP32-C3) is the lead target — trivial context switch, the
      32-bit inference path is one --cc flag away. xtensa follows.
- D2: "No Python" means on-target. Host-side idf.py/esptool convenience is
      acceptable; a Python-free host build is a stretch goal, not a rule.
- D3: Single-task Ruby. No second FreeRTOS task ever touches Ruby values.
