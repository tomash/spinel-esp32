#!/bin/bash
# tools/build-app.sh -- compile a Ruby app through spinel to native code.
#
# Host mode: full Ruby -> C -> host binary -> run (proves the pipeline).
# ESP32 mode: emit C for the ESP32 target; with --flash/--monitor also copy
# it into firmware/, cross-compile with idf.py, flash /dev/ttyUSB0, and
# optionally capture serial output via the DTR/RTS reset sequencing.
#
# Usage:
#   tools/build-app.sh app/hello.rb                       # host compile + run
#   tools/build-app.sh app/hello.rb --emit-only           # stop after C generation
#   tools/build-app.sh app/hello.rb --target=esp32        # emit C (esp32 target)
#   tools/build-app.sh app/hello.rb --target=esp32c3      # emit C (C3, same as esp32 today)
#   tools/build-app.sh app/hello.rb --target=esp32 --flash    # + copy, idf.py build & flash
#   tools/build-app.sh app/hello.rb --target=esp32 --flash --monitor  # + capture serial output
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SPINEL="$ROOT/vendor/spinel/spinel"
OUT="$ROOT/build"
IDF_EXPORT="$HOME/esp/esp-idf-v5.5/export.sh"
# Board targets: esp32c3 (T-OI Plus, /dev/ttyUSB0, default) and esp32
# (classic Xtensa, T-Display, usually /dev/ttyUSB1). ttyUSB order is not
# stable across reboots -- SPINEL_ESP_PORT always wins.
PORT="${SPINEL_ESP_PORT:-/dev/ttyUSB0}"
MONITOR="${SPINEL_ESP_MONITOR:-$ROOT/tools/monitor.py}"
SECONDS_OUT="${SPINEL_ESP_CAPTURE:-12}"

rb="${1:?usage: build-app.sh <file.rb> [--emit-only|--target=esp32|--flash|--monitor]}"
shift || true
mode=""
want_flash=0
want_monitor=0
TARGET="esp32c3"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --emit-only)   mode="--emit-only" ;;
    --target=esp32) mode="--target=esp32"; TARGET="esp32" ;;
    --target=esp32c3) mode="--target=esp32"; TARGET="esp32c3" ;;
    --flash)       want_flash=1 ;;
    --monitor)     want_monitor=1; want_flash=1 ;;
    *) echo "build-app.sh: unknown arg $1" >&2; exit 2 ;;
  esac
  shift
done
base="$(basename "$rb" .rb)"
mkdir -p "$OUT"

# The gcc spinel probes for the target ABI (32-bit int inference). Per target:
XGCC=""
case "$TARGET" in
  esp32c3)  XGCC="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc" ;;
  # Chip-specific driver (xtensa-esp32-elf-gcc), not the generic
  # xtensa-esp-elf-gcc: the generic driver's default multilib is big-endian,
  # which mismatches IDF's esp32 config (linker rejects the archive).
  esp32)    XGCC="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc" ;;
esac

echo "==> spinel C generation: $rb"
# NB: -S writes generated C to stdout; -o only names the binary output.
# --target=esp32: --cc= the cross gcc, so spinel's popen(-E -dM) probe reads
# __SIZEOF_POINTER__=4 and infers 32-bit ints (sp_int=long long stays 64).
if [[ "$mode" == "--target=esp32" || $want_flash -eq 1 || $want_monitor -eq 1 ]]; then
  "$SPINEL" "$rb" -S --cc="$XGCC" > "$OUT/$base.c"
  echo "==> emitted $OUT/$base.c ($(wc -l < "$OUT/$base.c") lines) -- target=esp32"
  if [[ $want_flash -eq 0 && $want_monitor -eq 0 ]]; then
    exit 0
  fi

  echo "==> copy $OUT/$base.c -> firmware/main/app.c"
  cp "$OUT/$base.c" "$ROOT/firmware/main/app.c"

  # WiFi credentials -> firmware/main/wifi_secrets.h (gitignored). Taken
  # from the environment, else from a top-level .env; an existing header
  # is left alone when neither sets them.
  if [[ -z "${WIFI_SSID:-}" && -f "$ROOT/.env" ]]; then
    set -a; source "$ROOT/.env"; set +a
  fi
  if [[ -n "${WIFI_SSID:-}" ]]; then
    c_str() { local s="${1//\\/\\\\}"; printf '"%s"' "${s//\"/\\\"}"; }
    printf '#define WIFI_SSID     %s\n#define WIFI_PASSWORD %s\n' \
      "$(c_str "$WIFI_SSID")" "$(c_str "${WIFI_PASSWORD:-}")" \
      > "$ROOT/firmware/main/wifi_secrets.h"
    echo "==> wrote firmware/main/wifi_secrets.h (SSID $WIFI_SSID)"
  fi

  echo "==> idf.py set-target $TARGET (regenerates sdkconfig from defaults)"
  ( cd "$ROOT/firmware" && source "$IDF_EXPORT" >/dev/null 2>&1 \
      && idf.py set-target "$TARGET" )

  echo "==> idf.py build (ESP-IDF $(basename "$(dirname "$IDF_EXPORT")"))"
  # shellcheck disable=SC1090
  ( cd "$ROOT/firmware" && source "$IDF_EXPORT" >/dev/null 2>&1 \
      && idf.py build )
  echo "==> image: $(ls -l "$ROOT/firmware/build/spinel_esp32_app.bin" | awk '{print $5" bytes"}')"

  echo "==> idf.py -p $PORT flash"
  ( cd "$ROOT/firmware" && source "$IDF_EXPORT" >/dev/null 2>&1 \
      && idf.py -p "$PORT" flash )
  echo "==> flashed ok"

  if [[ $want_monitor -eq 1 ]]; then
    if [[ ! -x "$MONITOR" ]]; then
      echo "!! monitor helper $MONITOR not found/executable -- skipping capture" >&2
      exit 0
    fi
    echo "==> serial capture ${SECONDS_OUT}s (DTR/RTS reset, $PORT) -> $OUT/$base.log"
    SPINEL_ESP_PORT="$PORT" SPINEL_ESP_CAPTURE="$SECONDS_OUT" \
      python3 "$MONITOR" > "$OUT/$base.log" 2>&1
    echo "==> captured: $OUT/$base.log"
    grep -av '^$' "$OUT/$base.log" | tail -n 15
  fi
  exit 0
fi

"$SPINEL" "$rb" -S > "$OUT/$base.c"

if [[ "$mode" == "--emit-only" ]]; then
  echo "==> emitted $OUT/$base.c ($(wc -l < "$OUT/$base.c") lines) -- stopping (emit-only)"
  exit 0
fi

echo "==> host compile (gcc, static-ish for size realism)"
gcc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$ROOT/vendor/spinel/lib" -I"$ROOT/vendor/spinel/lib/regexp" \
    "$OUT/$base.c" "$ROOT/vendor/spinel/lib/libspinel_rt.a" -lm -lpthread \
    -o "$OUT/$base"

echo "==> run"
"$OUT/$base"
echo "==> ok: $OUT/$base"
