# spinel-esp32

**Ruby → native ESP32 firmware, via [matz/spinel](https://github.com/matz/spinel).**

No interpreter on target. No Python *on target*. No mruby, no picoruby, no mruby-esp32.
Spinel compiles whole Ruby programs through whole-program type inference to plain C,
then to a native binary. This project ports that pipeline to ESP32 (FreeRTOS/ESP-IDF).

## The pipeline

```
app/*.rb ──spinel -S──▶ build/app.c ──┐
                                      ├── cross-cc + libspinel_rt (esp32) ──▶ firmware.elf/bin
libspinel_rt.a (built for xtensa/riscv32, shimmed for FreeRTOS) ──┘
```

1. Host-side `spinel` emits C for the app (`--cc=<cross-gcc>` makes inference 32-bit-aware).
2. Spinel's runtime (`vendor/spinel/lib/`) is rebuilt for the ESP toolchain against a
   FreeRTOS/newlib compatibility shim (`shim/esp32/`), mirroring the in-tree
   `lib/wasi/` precedent (a constrained target done via header shims).
3. The generated C + runtime + app FFI bindings link into one `.elf`, flash like any
   ESP-IDF app. Ruby code runs as a single FreeRTOS task owning all GC state.

## Repo layout

```
app/          Ruby applications (the actual firmware logic)
docs/         findings.md (runtime port research), plan.md (milestones)
shim/esp32/   FreeRTOS/newlib compatibility shim for libspinel_rt (the core work)
patches/      patches carried against vendor/spinel (series-style)
tools/        build pipeline scripts (host compile, cross compile, flash)
vendor/       git submodules (spinel)
```

## Status

See `docs/plan.md`. M0 (this commit): host pipeline proven end-to-end in-repo;
ESP32 port not started yet.

## Constraints honored

- Target binary: pure C compile of Ruby source. No Ruby VM, no parser, no interpreter, no Python.
- Host tooling: spinel (C binary) + a C cross-compiler. ESP-IDF's Python-based build
  drivers (idf.py/esptool) are host-side conveniences only — see docs/plan.md M1 decision.

## License

MIT (matches upstream spinel).
