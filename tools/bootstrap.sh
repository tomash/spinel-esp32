#!/bin/bash
# tools/bootstrap.sh -- one-time setup for building spinel-esp32 firmware.
#
# Turns "clone + README archaeology" into a single command:
#
#   git clone https://github.com/tomash/spinel-esp32.git
#   cd spinel-esp32 && make setup
#
# Steps (each skipped if already satisfied):
#   1. vendor/spinel submodule at the pinned commit (SPINEL_PIN)
#   2. spinel's prism/rbs sources  (make deps -> rubygems.org)
#   3. the host spinel compiler     (make -C vendor/spinel)
#   4. the ESP32 cross runtime      (prebuilt download, or build from source)
#
# The cross runtime (libspinel_rt_esp32.a) is the slow part (~7 min). It is
# published as a release asset keyed by the spinel pin, so a fresh setup
# downloads it instead of cross-compiling. Set SPINEL_RT_FROM_SOURCE=1 to
# always build it, or SPINEL_RT_NO_DOWNLOAD=1 to never download.
#
# Env overrides: ESP_IDF, SPINEL_RT_BASE_URL, SPINEL_RT_FROM_SOURCE,
#                SPINEL_RT_NO_DOWNLOAD, SPINEL_SKIP_IDF
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PIN="$(tr -d '[:space:]' < SPINEL_PIN)"
RT_BASE="${SPINEL_RT_BASE_URL:-https://github.com/tomash/spinel-esp32/releases/download/spinel-rt-$PIN}"
SKIP_IDF="${SPINEL_SKIP_IDF:-0}"

say() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---- 0. host tools --------------------------------------------------------
say "checking host tools"
missing=""
for t in git make cc; do have "$t" || missing="$missing $t"; done
if [ -n "$missing" ]; then
  echo "error: missing host tools:$missing" >&2
  echo "  Ubuntu/Debian: sudo apt-get install -y build-essential git wget curl ruby" >&2
  echo "  macOS:         xcode-select --install && brew install ruby wget" >&2
  exit 1
fi
have ruby || echo "warning: ruby not found -- 'make deps' needs it (spinel's Makefile uses ruby to locate prism; install it if the deps step fails)"

# ---- 1. submodule ---------------------------------------------------------
say "submodule vendor/spinel"
if [ ! -s vendor/spinel/Makefile ]; then
  git submodule update --init --recursive
fi
got="$(git -C vendor/spinel rev-parse HEAD 2>/dev/null || echo none)"
if [ "$got" != "$PIN" ]; then
  echo "  checking out pinned commit $PIN (was ${got:0:9})"
  git -C vendor/spinel fetch --tags origin >/dev/null 2>&1 || true
  git -C vendor/spinel checkout --quiet "$PIN"
fi
echo "  vendor/spinel @ $(git -C vendor/spinel rev-parse --short HEAD)  ($(git -C vendor/spinel describe --tags 2>/dev/null || echo 'no tags'))"

# ---- 2+3. the host spinel compiler ---------------------------------------
if [ -x vendor/spinel/bin/spinel ]; then
  say "host spinel already built"
else
  say "fetching spinel deps (prism + rbs from rubygems.org)"
  make -C vendor/spinel deps
  say "building host spinel (this takes a few minutes)"
  make -C vendor/spinel -j"$(nproc 2>/dev/null || echo 2)"
fi
"$ROOT/vendor/spinel/bin/spinel" --version 2>/dev/null | head -1 || true

# ---- 4. ESP32 cross toolchain --------------------------------------------
IDF="${ESP_IDF:-$HOME/esp/esp-idf-v5.5}"
if [ "$SKIP_IDF" = "1" ]; then
  say "skipping ESP-IDF check (SPINEL_SKIP_IDF=1)"
elif [ -d "$IDF" ]; then
  say "ESP-IDF found at $IDF"
else
  say "ESP-IDF not found at $IDF"
  cat <<EOF
  ESP-IDF is the cross toolchain (riscv32-esp-elf-gcc + headers) and is only
  needed to build/flash firmware, not to run the host pipeline. To install it:

    sudo apt-get install -y git wget flex bison gperf python3 python3-pip \\
      python3-venv cmake ninja-build ccache libffi-dev libssl-dev \\
      dfu-util libusb-1.0-0
    mkdir -p ~/esp && cd ~/esp
    git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5
    cd esp-idf-v5.5 && ./install.sh esp32c3

  ~4 GB clone + ~1.5 GB toolchains; re-run 'make setup' afterwards.
  (Skip this check with SPINEL_SKIP_IDF=1 if you only want the host pipeline.)
EOF
fi

# ---- 5. cross runtime -----------------------------------------------------
rt="$ROOT/build/rt-riscv32/libspinel_rt_esp32.a"
say "ESP32 cross runtime (libspinel_rt_esp32.a)"
if [ -f "$rt" ]; then
  echo "  present: $rt"
elif [ "${SPINEL_RT_FROM_SOURCE:-0}" != "1" ] && [ "${SPINEL_RT_NO_DOWNLOAD:-0}" != "1" ]; then
  url="$RT_BASE/libspinel_rt_esp32_riscv32.a"
  echo "  downloading prebuilt for pin ${PIN:0:9}: $url"
  mkdir -p "$(dirname "$rt")"
  if curl -fsSL -o "$rt.tmp" "$url"; then
    mv "$rt.tmp" "$rt"
    echo "  ok ($(wc -c < "$rt") bytes)"
  else
    rm -f "$rt.tmp"
    echo "  no prebuilt asset for this pin -- building from source instead"
    SPINEL_RT_FROM_SOURCE=1
  fi
fi
if [ ! -f "$rt" ]; then
  if [ -d "$IDF" ]; then
    say "cross-compiling the runtime (~7 min, once)"
    ESP_IDF="$IDF" "$ROOT/tools/build-rt.sh" riscv32
  else
    echo "  !! cannot build the runtime without ESP-IDF; skipping" >&2
  fi
fi

say "done"
echo "  host run:    tools/build-app.sh app/hello.rb"
echo "  emit C:      tools/build-app.sh app/blink.rb --target=esp32"
echo "  build+flash: tools/build-app.sh app/blink.rb --target=esp32 --flash --monitor"
echo "  (or the Makefile: make run / make emit APP=app/blink.rb / make flash APP=app/blink.rb)"
