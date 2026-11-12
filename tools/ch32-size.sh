#!/bin/bash
# tools/ch32-size.sh -- bare-metal footprint of Ruby apps on rv32imac.
#
# No ESP-IDF, no RTOS: the generated app TU + libspinel_rt + newlib-nano +
# libnosys, linked with baremetal/measure.ld (oversized regions) and checked
# against a budget (default CH32V203C8: 64 KB flash / 20 KB RAM). This is
# the number a CH32V2/V3-class port has to fit; the stack and GC heap come
# out of whatever RAM is left.
#
# Usage:
#   tools/ch32-size.sh [app.rb ...]          # default: empty + ch32/blink
#
# Environment:
#   RT_DIR=build/rt-riscv32imac-small-os     runtime archive dir
#   SPINEL_SRC=...                           spinel tree (spinel binary + lib/)
#   SPINEL_BIN=...                           spinel compiler (default $SPINEL_SRC/spinel)
#   SPINEL_PRESET=small                      presets/<name>.defs for the app TU
#   SPINEL_FLAGS=--no-bigint                 extra spinel flags
#   APP_OPT=-Os                              app TU optimisation
#   LINK_FLAGS="-flto -Os"                   extra link flags (LTO needs the opt level here too)
#   FLASH_KB=64 RAM_KB=20                    budget
#   TOP=15                                   rows in the per-object breakdown
#   EXTRA_SRCS="baremetal/tinystdio.c"        C files linked ahead of newlib (board layer)
#   APP_DEFS="-DSP_NO_SLAB"                  extra flags for the app TU (match the runtime's switches)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SPINEL_SRC="${SPINEL_SRC:-$ROOT/vendor/spinel}"
SPINEL_BIN="${SPINEL_BIN:-$SPINEL_SRC/spinel}"   # compiler binary (may come from another tree at the same commit)
RT_DIR="${RT_DIR:-$ROOT/build/rt-riscv32imac-small-os}"
PRESET="${SPINEL_PRESET:-small}"
FLAGS="${SPINEL_FLAGS:---no-bigint}"
FLASH_KB="${FLASH_KB:-64}" RAM_KB="${RAM_KB:-20}" TOP="${TOP:-15}"
BIN="$(dirname "$(ls ~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | tail -1)")"
CC="$BIN/riscv32-esp-elf-gcc" SIZE="$BIN/riscv32-esp-elf-size"
ARCH="-march=rv32imac_zicsr_zifencei -mabi=ilp32"
OUT="$ROOT/build/ch32"; mkdir -p "$OUT"
[[ $# -gt 0 ]] || set -- "$ROOT/app/size/empty.rb" "$ROOT/app/ch32/blink.rb"

DEFS="-DSP_NO_SIGNALS -D__ESP32_SHIM__"
DEFS="$DEFS $(grep -v '^#' "$ROOT/presets/$PRESET.defs" | grep . | sed 's/^/-D/' | tr '\n' ' ')"
for f in $(sed -n 's/^override COPT += *//p' "$SPINEL_SRC/Makefile" 2>/dev/null); do DEFS="$DEFS $f"; done
DEFS="$DEFS ${APP_DEFS:-}"
INC="-I$ROOT/shim/esp32/include -I$SPINEL_SRC/lib -I$SPINEL_SRC/lib/regexp"

"$CC" $ARCH -c "$ROOT/baremetal/crt0.S" -o "$OUT/crt0.o"
EXTRA_OBJS=""
for src in ${EXTRA_SRCS:-}; do
  o="$OUT/extra-$(basename "$src" .c).o"
  # shellcheck disable=SC2086
  "$CC" $ARCH ${APP_OPT:--Os} -ffunction-sections -fdata-sections -fno-builtin -c "$ROOT/$src" -o "$o"
  EXTRA_OBJS="$EXTRA_OBJS $o"
done
for rb in "$@"; do
  b="$(basename "$rb" .rb)"
  # shellcheck disable=SC2086
  "$SPINEL_BIN" "$rb" -S --cc="$CC" $FLAGS > "$OUT/$b.c"
  # shellcheck disable=SC2086
  "$CC" $ARCH ${APP_OPT:--Os} -ffunction-sections -fdata-sections -w $INC $DEFS -c "$OUT/$b.c" -o "$OUT/$b.o"
  "$CC" $ARCH ${LINK_FLAGS:-} -nostartfiles -specs=nano.specs -T "$ROOT/baremetal/measure.ld" \
    -Wl,--gc-sections -Wl,-Map="$OUT/$b.map" "$OUT/crt0.o" "$OUT/$b.o" $EXTRA_OBJS \
    "$RT_DIR/libspinel_rt_esp32.a" -lm -lc_nano -lnosys -o "$OUT/$b.elf" >"$OUT/$b.link.log" 2>&1
  read -r text data bss _ < <("$SIZE" "$OUT/$b.elf" | tail -1)
  python3 - "$OUT/$b.map" "$b" "$text" "$data" "$bss" "$FLASH_KB" "$RAM_KB" "$TOP" <<'EOF'
import re, sys, collections
mp, name, text, data, bss, fkb, rkb, top = sys.argv[1:]
text, data, bss, fkb, rkb, top = map(int, (text, data, bss, fkb, rkb, top))
flash, ram = text + data, data + bss
ok = lambda used, kb: "fits" if used <= kb * 1024 else f"OVER by {used - kb*1024}"
print(f"== {name}: flash {flash} B ({flash/1024:.1f} KB, {ok(flash,fkb)}), "
      f"static RAM {ram} B ({ram/1024:.1f} KB, {ok(ram,rkb)}; {rkb*1024-ram} B left for stack+heap)")
m = open(mp).read(); m = m[m.index('Linker script and memory map'):]
fl, rm = collections.Counter(), collections.Counter()
for sec, size, src in re.findall(r'^ (\.\S+)\s*\n?\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)', m, re.M):
    key = re.sub(r'.*/', '', src)
    if sec.startswith(('.text', '.rodata', '.srodata')): fl[key] += int(size, 16)
    elif sec.startswith(('.bss', '.sbss', '.data', '.sdata')): rm[key] += int(size, 16)
grp = collections.Counter()
for k, v in fl.items():
    g = 'spinel rt' if 'spinel_rt' in k else 'libc/libm' if k.startswith(('libc', 'libm')) \
        else 'libgcc (soft-float, 64-bit div)' if k.startswith('libgcc') else 'app TU' if k.endswith('.o)') is False and k.endswith('.o') else 'other'
    grp[g] += v
print("   flash by origin: " + ", ".join(f"{g} {v}" for g, v in grp.most_common()))
print("   flash top:", ", ".join(f"{re.sub(r'.*[(]|[.]o[)]$', '', k)} {v}" for k, v in fl.most_common(top)))
print("   RAM top:  ", ", ".join(f"{re.sub(r'.*[(]|[.]o[)]$', '', k)} {v}" for k, v in rm.most_common(8)))
EOF
done
