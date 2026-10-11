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

rb="${1:?usage: build-app.sh <file.rb> [--emit-only]}"
mode="${2:-}"
base="$(basename "$rb" .rb)"
mkdir -p "$OUT"

echo "==> spinel C generation: $rb"
# NB: -S writes generated C to stdout; -o only names the binary output.
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
