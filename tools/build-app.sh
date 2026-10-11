#!/bin/bash
# tools/build-app.sh -- compile a Ruby app through spinel to native code (HOST path).
#
# Host-stage of the ESP32 pipeline: proves the Ruby -> C step and produces
# the C we will later cross-compile. TARGET=esp32 changes only the cc probe
# (inference word size); linking against the ESP runtime comes in M1.
#
# Usage:
#   tools/build-app.sh app/hello.rb              # host compile + run
#   tools/build-app.sh app/hello.rb --emit-only   # stop after C generation
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SPINEL="$ROOT/vendor/spinel/spinel"
OUT="$ROOT/build"

rb="${1:?usage: build-app.sh <file.rb> [--emit-only|--target=esp32]}"
shift || true
mode=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --emit-only)   mode="--emit-only" ;;
    --target=esp32) mode="--target=esp32" ;;
    *) echo "build-app.sh: unknown arg $1" >&2; exit 2 ;;
  esac
  shift
done
base="$(basename "$rb" .rb)"
mkdir -p "$OUT"

XGCC="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc"

echo "==> spinel C generation: $rb"
# NB: -S writes generated C to stdout; -o only names the binary output.
# --target=esp32: --cc= the cross gcc, so spinel's popen(-E -dM) probe reads
# __SIZEOF_POINTER__=4 and infers 32-bit ints (sp_int=long long stays 64).
if [[ "$mode" == "--target=esp32" ]]; then
  "$SPINEL" "$rb" -S --cc="$XGCC" > "$OUT/$base.c"
  echo "==> emitted $OUT/$base.c ($(wc -l < "$OUT/$base.c") lines) -- target=esp32"
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
