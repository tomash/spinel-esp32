#!/bin/bash
# tools/board-flash.sh -- build an app for a named board, flash it, capture
# its serial output. Board -> target / port / baud comes from boards.local
# (see boards.local.example).
#
#   tools/board-flash.sh m5stickc app/board_info.rb [capture_seconds]
#
# Everything else (SPINEL_PRESET, SPINEL_FLAGS, SPINEL_SRC) passes through
# to tools/build-app.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
board="${1:?usage: board-flash.sh <board> <app.rb> [capture_s]}"
app="${2:?usage: board-flash.sh <board> <app.rb> [capture_s]}"
cap="${3:-15}"
line="$(grep -E "^$board[[:space:]]" "$ROOT/boards.local" 2>/dev/null || true)"
[[ -n "$line" ]] || { echo "board-flash.sh: no '$board' in boards.local" >&2; exit 2; }
read -r _ target port baud <<<"$line"
echo "==> $board: target $target, port $port${baud:+, ${baud} baud}"
if [[ "$target" == rp2350 ]]; then
  # Pico 2 W: picotool flashes over USB, the port is the app's USB serial.
  SPINEL_BOARD="$board" SPINEL_ESP_PORT="$port" SPINEL_ESP_CAPTURE="$cap" \
    "$ROOT/tools/build-app.sh" "$app" --target=rp2350 --monitor
  exit
fi
if [[ "$target" != esp32s3 ]]; then
  SPINEL_BOARD="$board" SPINEL_ESP_PORT="$port" SPINEL_ESP_CAPTURE="$cap" \
    env ${baud:+ESPBAUD=$baud} "$ROOT/tools/build-app.sh" "$app" --target="$target" --monitor
  exit
fi
# ESP32-S3 over its own USB-Serial/JTAG (M5StickS3): the RTS reset that ends
# a flash, and tools/monitor.py's DTR/RTS reset, can land the chip back in
# download mode (the PMIC keeps the BOOT strap low after a power-button
# download entry). So: let esptool enter download mode itself (works whether
# the app or the ROM is running), leave it with the RTC-watchdog reset, which
# boots the app; then read the console without touching DTR/RTS.
SPINEL_BOARD="$board" SPINEL_ESP_PORT="$port" \
  env ${baud:+ESPBAUD=$baud} "$ROOT/tools/build-app.sh" "$app" --target="$target" --flash
base="$(basename "$app" .rb)"
( source "$HOME/esp/esp-idf-v5.5/export.sh" >/dev/null 2>&1 \
  && python -m esptool --port "$port" --before default_reset --after watchdog_reset chip_id >/dev/null ) \
  || echo "!! watchdog reset failed; the app may still be running from the flash reset"
for _ in $(seq 1 20); do [[ -e "$port" ]] && break; sleep 0.5; done
echo "==> serial capture ${cap}s (no reset, $port) -> build/$base.log"
python3 - "$port" "$cap" > "$ROOT/build/$base.log" <<'PY'
import serial, sys, time
s = serial.Serial(); s.port = sys.argv[1]; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False; s.open()
end = time.time() + float(sys.argv[2])
while time.time() < end:
    sys.stdout.buffer.write(s.read(4096)); sys.stdout.flush()
PY
grep -av '^$' "$ROOT/build/$base.log" | tail -n 15
