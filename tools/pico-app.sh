#!/bin/bash
# tools/pico-app.sh -- the Pico SDK leg of tools/build-app.sh (--target=rp2350):
# Ruby -> C (spinel, probing the Pico RISC-V GCC) -> firmware-pico/ build ->
# picotool load -> USB serial capture.
#
#   tools/pico-app.sh app/hello.rb [--build|--flash|--monitor]
#
# Toolchain (tools/build-rt.sh rp2350-riscv needs the same GCC):
#   PICO_SDK_PATH        default ~/pico/pico-sdk
#   PICO_TOOLCHAIN_PATH  default ~/pico/riscv-gcc  (riscv32-pico-elf-*)
#   PICOTOOL             default ~/pico/picotool-bin/picotool/picotool
# Board: SPINEL_ESP_PORT is the USB serial port the app's stdio enumerates as
# (default: the first /dev/serial/by-id/usb-Raspberry_Pi_*); SPINEL_ESP_CAPTURE
# the capture window in seconds. SPINEL_PRESET/SPINEL_FLAGS/SPINEL_SRC/
# SPINEL_BOARD pass through as for the ESP firmware.
#
# Flashing needs USB access to the RP2350 (tools/udev/60-spinel-mote-rp2.rules).
# picotool reaches a board in BOOTSEL directly, and one running a spinel-mote
# app through the USB reset interface its stdio exposes (load -f), so after the
# first flash no button press is needed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SPINEL="${SPINEL_SRC:-$ROOT/vendor/spinel}/spinel"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico/pico-sdk}"
export PICO_TOOLCHAIN_PATH="${PICO_TOOLCHAIN_PATH:-$HOME/pico/riscv-gcc}"
PICOTOOL="${PICOTOOL:-$HOME/pico/picotool-bin/picotool/picotool}"
export picotool_DIR="${picotool_DIR:-$(dirname "$PICOTOOL")}"
XGCC="$PICO_TOOLCHAIN_PATH/bin/riscv32-pico-elf-gcc"
FW="$ROOT/firmware-pico"
BUILD="$FW/build"
CAP="${SPINEL_ESP_CAPTURE:-12}"

rb="${1:?usage: pico-app.sh <file.rb> [--build|--flash|--monitor]}"
shift
want_flash=0; want_monitor=0
for a in "$@"; do
  case "$a" in
    --build) ;;
    --flash) want_flash=1 ;;
    --monitor) want_flash=1; want_monitor=1 ;;
    *) echo "pico-app.sh: unknown arg $a" >&2; exit 2 ;;
  esac
done
base="$(basename "$rb" .rb)"
mkdir -p "$ROOT/build"

echo "==> spinel C generation: $rb (rp2350-riscv)"
# shellcheck disable=SC2086
"$SPINEL" "$rb" -S --cc="$XGCC" ${SPINEL_FLAGS:-} > "$ROOT/build/$base.c"
cp "$ROOT/build/$base.c" "$FW/main/app.c"

# Variant settings are read at configure time: reconfigure every run (cheap).
echo "==> cmake + ninja (firmware-pico)"
cmake -S "$FW" -B "$BUILD" -G Ninja >/dev/null
rm -f "$BUILD/spinel_mote_app.uf2" "$BUILD/spinel_mote_app.elf"
ninja -C "$BUILD" -j "${JOBS:-2}" | grep -Ev '^\[[0-9]+/[0-9]+\] (Building|Linking)' || true
[[ -f "$BUILD/spinel_mote_app.uf2" ]] || { echo "!! build failed" >&2; exit 1; }
"$PICO_TOOLCHAIN_PATH/bin/riscv32-pico-elf-size" "$BUILD/spinel_mote_app.elf"
[[ $want_flash -eq 1 ]] || exit 0

echo "==> picotool load (reboots a running app into BOOTSEL first)"
"$PICOTOOL" load -f -x -v "$BUILD/spinel_mote_app.uf2" 2>&1 | tr "\r" "\n" | grep -v "%\]"
[[ $want_monitor -eq 1 ]] || exit 0

port="${SPINEL_ESP_PORT:-}"
for _ in $(seq 1 40); do
  if [[ -z "${SPINEL_ESP_PORT:-}" ]]; then
    port="$(ls /dev/serial/by-id/usb-Raspberry_Pi_* 2>/dev/null | head -1 || true)"
  fi
  [[ -n "$port" && -e "$port" ]] && break
  sleep 0.25
done
[[ -n "$port" && -e "$port" ]] || { echo "!! no USB serial port appeared" >&2; exit 1; }
echo "==> serial capture ${CAP}s ($port) -> build/$base.log"
# The app waits (up to 5 s) for the port to be opened before it runs, so
# opening it is enough: no reset, nothing is lost.
python3 - "$port" "$CAP" > "$ROOT/build/$base.log" <<'PY'
import serial, sys, time
end = time.time() + float(sys.argv[2])
s = serial.Serial(sys.argv[1], 115200, timeout=0.2)
while time.time() < end:
    b = s.read(4096)
    if b:
        sys.stdout.write(b.decode("utf-8", "replace")); sys.stdout.flush()
PY
grep -av '^$' "$ROOT/build/$base.log" | tail -n 25
