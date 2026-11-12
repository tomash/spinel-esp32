#!/bin/bash
# tools/size-report.sh -- footprint of a Ruby app on target, split by origin.
#
# Builds each app into its own IDF build dir (firmware/build-size-<app>,
# own sdkconfig) so the regular firmware/build is left alone, then splits
# the link map into: spinel runtime (libspinel_rt), the generated app TU,
# and everything else (ESP-IDF, newlib, FreeRTOS). Flash and static RAM
# (.data/.bss) are reported separately, and the top static-RAM objects of
# the runtime are listed -- that number gates the small-MCU tiers.
#
# Usage:
#   tools/size-report.sh [--target=esp32c3|esp32|esp32c6|esp32s3] app/size/empty.rb app/blink.rb ...
#
# Writes build/size/<app>-<target>[-<variant>].txt per app.
#
# Variants (environment; see firmware/spinel_target.cmake):
#   VARIANT=small          label for build dir and report names
#   SPINEL_SRC=...         spinel tree: its spinel binary emits the C, its
#                          lib/ headers compile it (default vendor/spinel)
#   SPINEL_FLAGS="--no-bigint"   extra spinel flags
#   SPINEL_RT_SUFFIX=-small      runtime archive build/rt-<arch><suffix>
#   SPINEL_APP_DEFS="SP_NO_ALLOC_REPORT"   extra app TU defines
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SPINEL="${SPINEL_SRC:-$ROOT/vendor/spinel}/spinel"
VSUF="${VARIANT:+-$VARIANT}"
IDF_EXPORT="$HOME/esp/esp-idf-v5.5/export.sh"
TARGET="esp32c3"
apps=()
for a in "$@"; do
  case "$a" in
    --target=*) TARGET="${a#--target=}" ;;
    *) apps+=("$a") ;;
  esac
done
[[ ${#apps[@]} -gt 0 ]] || { echo "usage: size-report.sh [--target=..] app.rb..." >&2; exit 2; }

case "$TARGET" in
  esp32c3) XGCC="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc" ;;
  esp32)   XGCC="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc" ;;
  esp32c6) XGCC="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc" ;;
  esp32s3) XGCC="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc" ;;
  *) echo "size-report.sh: unknown target $TARGET" >&2; exit 2 ;;
esac

mkdir -p "$ROOT/build/size"
# The WiFi apps #error without firmware/main/wifi_secrets.h (gitignored);
# write it from the environment or .env as tools/build-app.sh does, and leave
# an existing one alone. Credentials do not change the footprint.
if [[ ! -f "$ROOT/firmware/main/wifi_secrets.h" ]]; then
  if [[ -z "${WIFI_SSID:-}" && -f "$ROOT/.env" ]]; then set -a; source "$ROOT/.env"; set +a; fi
  c_str() { local v="${1//\\/\\\\}"; printf '"%s"' "${v//\"/\\\"}"; }
  printf '#define WIFI_SSID     %s\n#define WIFI_PASSWORD %s\n' \
    "$(c_str "${WIFI_SSID:-size-report}")" "$(c_str "${WIFI_PASSWORD:-}")" > "$ROOT/firmware/main/wifi_secrets.h"
fi
APP_C="$ROOT/firmware/main/app.c"
had_app_c=0
if [[ -f "$APP_C" ]]; then cp "$APP_C" "$ROOT/build/size/.app.c.keep"; had_app_c=1; fi
restore() {
  if [[ $had_app_c -eq 1 ]]; then mv "$ROOT/build/size/.app.c.keep" "$APP_C"; else rm -f "$APP_C"; fi
}
trap restore EXIT

for rb in "${apps[@]}"; do
  base="$(basename "$rb" .rb)"
  bdir="build-size-$base-$TARGET$VSUF"
  rep="$base-$TARGET$VSUF"
  echo "==> $rb ($TARGET${VARIANT:+, variant $VARIANT})"
  # shellcheck disable=SC2086
  "$SPINEL" "$ROOT/$rb" -S --cc="$XGCC" ${SPINEL_FLAGS:-} > "$APP_C" \
    || { echo "!! spinel refused $rb" >&2; continue; }
  # shellcheck disable=SC1090
  ( cd "$ROOT/firmware" && source "$IDF_EXPORT" >/dev/null 2>&1 \
      && IDF_TARGET="$TARGET" idf.py -B "$bdir" -D SDKCONFIG="$bdir/sdkconfig" \
           -D IDF_TARGET="$TARGET" build >"$ROOT/build/size/$rep.build.log" 2>&1 ) \
    || { echo "!! build failed, see build/size/$rep.build.log" >&2; continue; }
  python3 - "$ROOT/firmware/$bdir/spinel_esp32_app.map" "$ROOT/firmware/$bdir/spinel_esp32_app.bin" \
      > "$ROOT/build/size/$rep.txt" <<'PY'
import os, re, sys, collections
mapf, binf = sys.argv[1], sys.argv[2]
lines = open(mapf, errors="replace").read().splitlines()
# Input sections appear as " .sec.name  0xADDR 0xSIZE origin" (one line) or
# " .sec.name" followed by "  0xADDR 0xSIZE origin" (long names wrap).
row = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
one = re.compile(r"^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
start = next((i for i, l in enumerate(lines) if l.startswith("Linker script and memory map")), 0)
tot = collections.Counter()
bss_objs = collections.Counter()
pending = None
for l in lines[start:]:
    m = one.match(l)
    if m:
        sec, addr, size, origin = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        pending = None
    else:
        r = row.match(l)
        if r and pending:
            sec, addr, size, origin = pending, int(r.group(1), 16), int(r.group(2), 16), r.group(3)
            pending = None
        else:
            pending = l.strip() if re.match(r"^ \.\S+$", l) else None
            continue
    if addr == 0 or size == 0 or sec.startswith((".debug", ".comment", ".note", ".riscv.attributes", ".xtensa.info")):
        continue
    if "libspinel_rt" in origin:
        who = "spinel runtime"
    elif origin.endswith("app.c.obj") or "app.c.obj)" in origin:
        who = "app (generated C)"
    else:
        who = "ESP-IDF/libc/other"
    kind = "bss" if (".bss" in sec or "COMMON" in sec) else ("data" if ".data" in sec else "flash")
    tot[(who, kind)] += size
    if who == "spinel runtime" and kind == "bss":
        obj = re.search(r"\((.*)\)", origin)
        bss_objs[sec + "  " + (obj.group(1) if obj else origin)] += size
print(f"image (.bin): {os.path.getsize(binf)} bytes")
print(f"{'origin':22} {'flash':>9} {'.data':>8} {'.bss':>8}")
for who in ("spinel runtime", "app (generated C)", "ESP-IDF/libc/other"):
    print(f"{who:22} {tot[(who,'flash')]:9d} {tot[(who,'data')]:8d} {tot[(who,'bss')]:8d}")
print("\ntop spinel runtime .bss:")
for k, v in bss_objs.most_common(12):
    print(f"  {v:7d}  {k}")
PY
  cat "$ROOT/build/size/$rep.txt"
  echo
done
