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

## Prerequisites

A Linux host (tested on Ubuntu 24.04; Debian/Fedora/Arch equivalents work). Two
toolchains live side by side and never touch each other:

1. **The Ruby→C compiler ("spinel")** — built from the vendored source, runs on your
   host, needs nothing but a C compiler and Make.
2. **The ESP32 cross toolchain (ESP-IDF)** — the `riscv32-esp-elf-gcc` compiler and
   headers that compile the generated C (and the runtime) into firmware.

### 1. Host build tools (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential git wget curl ruby
```

`ruby` is needed only for `make deps` (spinel's Makefile uses it to locate the
prism parser sources it downloads from rubygems.org). Any modern system ruby works.

macOS: `xcode-select --install` covers the C toolchain; brew install ruby, wget.

### 2. Build the vendored spinel compiler

```bash
git clone https://github.com/tomash/spinel-esp32.git
cd spinel-esp32
git submodule update --init          # checks out vendor/spinel at the pinned commit
make -C vendor/spinel deps          # fetches prism v1.9.0 + rbs from rubygems (into vendor/)
make -C vendor/spinel               # builds bin/spinel + friends
```

Verify:

```bash
vendor/spinel/bin/spinel --version
# spinel 2026.09.12+8185 (6b6f4602c) [...]
```

Rebuild the runtime after patching is irrelevant for host builds; the cross
runtime is built separately (below).

### 3. Install ESP-IDF (the cross toolchain)

ESP-IDF brings `riscv32-esp-elf-gcc`, newlib headers, FreeRTOS, and the host-side
Python-based build driver (`idf.py`, esptool). **Python is only ever used on the
host as a build driver** — nothing Python ships on target; the firmware is a
pure C compile of the Ruby program.

```bash
# Ubuntu/Debian prerequisites (esp-idf's official list)
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0

# Clone ESP-IDF v5.5.5 --recursive (shallow submodules break the build)
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5
cd esp-idf-v5.5
./install.sh esp32c3          # installs riscv32 + xtensa gcc into ~/.espressif (~1.5 GB)
```

Notes:

- Disk: the full clone is ~2 GB, toolchains add ~1.5 GB under `~/.espressif`. 
  `/` should have ≥6 GB free before you start.
- To also target classic ESP32 / S3 (xtensa) later, run `./install.sh esp32 esp32s3`.
- The USB serial driver on the T-OI Plus is a CH340 — Linux kernels ≥3.x need no driver.

### 4. Put ESP-IDF in your environment (per shell)

```bash
. ~/esp/esp-idf-v5.5/export.sh
idf.py --version
```

Only needed in shells where you call `idf.py`/esptool directly. `tools/build-rt.sh`
sources it itself if it's not already active.

## Quick start: compile and run a Ruby program (host)

```bash
tools/build-app.sh app/hello.rb              # Ruby -> C -> host binary
./build/hello                                # runs the Ruby program, natively
tools/build-app.sh app/hello.rb --emit-only  # just emit build/hello.c, no compile
```

## Flashing to the T-OI Plus (ESP32-C3)

Connect the board over USB (CH340 serial). Then, with ESP-IDF exported:

```bash
tools/build-rt.sh          # cross-compiles libspinel_rt for riscv32-esp-elf (once)
idf.py -B build/esp32c3 -D SDKCONFIG=... -D IDF_TARGET=esp32c3 build flash monitor
```

(The exact `idf.py` invocation is wired into `tools/` scripts as they land; the
command above is the shape of it. Board details: docs/boards/t-oi-plus.md.)

## Repo layout

```
app/          Ruby applications (the actual firmware logic)
docs/         findings.md (runtime port research), plan.md (milestones), boards/
shim/esp32/   FreeRTOS/newlib compatibility shim for libspinel_rt (the core work)
patches/      patches carried against vendor/spinel (series-style)
tools/        build pipeline scripts (host compile, cross compile, flash)
vendor/       git submodules (spinel)
```

## Status

See `docs/plan.md`. M0 complete: host pipeline proven end-to-end in-repo.
M1 (cross runtime) in progress — shim design fully settled (zero upstream diffs,
wasi-pattern stubs; see docs/findings.md "Blockers" for the evidence).

## Constraints honored

- Target binary: pure C compile of Ruby source. No Ruby VM, no parser, no interpreter, no Python.
- Host tooling: spinel (C binary) + a C cross-compiler. ESP-IDF's Python-based build
  drivers (idf.py/esptool) are host-side conveniences only — see docs/plan.md M1 decision.

## License

MIT (matches upstream spinel).
