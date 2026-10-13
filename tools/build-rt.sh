#!/bin/bash
# tools/build-rt.sh -- cross-compile libspinel_rt.a for ESP32 (riscv32/xtensa).
#
# Mirrors the upstream Makefile's archive rule (ar rcs over RT_MEMBERS +
# regexp + platform objects) but with the ESP-IDF cross gcc and the
# shim/esp32 headers first on the include path. No upstream files are
# modified: the shim is -I'd in, and POSIX surface newlib lacks is defined
# to fail (the lib/wasi precedent).
#
# Usage:
#   tools/build-rt.sh                # riscv32 (esp32c3), default, needs IDF_PATH
#   tools/build-rt.sh esp32          # xtensa (classic ESP32, T-Display)
#   ESP_IDF=/path/to/esp-idf tools/build-rt.sh <target>
#   tools/build-rt.sh --probe        # check the cross libc surface (riscv32)
#   tools/build-rt.sh esp32 --probe  # check the xtensa cross libc surface
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDF="${ESP_IDF:-$HOME/esp/esp-idf-v5.5}"

# ---- target selection -----------------------------------------------------
# riscv32 -> ESP32-C3 (default, unchanged invocation), esp32 -> Xtensa classic.
TARGET=""
PROBE=0
for a in "$@"; do
  case "$a" in
    --probe) PROBE=1 ;;
    *) TARGET="$a" ;;
  esac
done
TARGET="${TARGET:-riscv32}"
case "$TARGET" in
  riscv32) GCC_NAME="riscv32-esp-elf-gcc" ;;
  # Chip-specific driver, NOT the generic xtensa-esp-elf-gcc: the generic
  # driver's default multilib is big-endian and the linker then rejects the
  # archive ("compiled for a big endian system"). The esp32 driver defaults
  # to the little-endian windowed ESP32 config.
  esp32)   GCC_NAME="xtensa-esp32-elf-gcc" ;;
  *) echo "error: unknown target '$TARGET' (riscv32|esp32)" >&2; exit 1 ;;
esac
AR_NAME="${GCC_NAME%gcc}ar"   # riscv32-esp-elf-ar / xtensa-esp-elf-ar

# ---- toolchain discovery -------------------------------------------------
if [[ ! -d "$IDF" ]]; then
  echo "error: ESP-IDF not found at $IDF (set ESP_IDF=...)" >&2
  exit 1
fi
# ESP-IDF's installer has used two layouts: ~/.espressif/tools/<name>/<ver>/<name>/
# (v5.5 install.sh) and ~/.espressif/frameworks/<name>-<ver>/ (older/idf-env).
# Search for the gcc binary itself so category dirs (e.g. tools/riscv32-esp-elf/)
# can't shadow the real prefix. Match the EXACT gcc name (pitfall: the xtensa
# and riscv32 toolchains live side by side under ~/.espressif; a loose
# '*-esp-elf-gcc' find can pick the wrong one).
CROSS=""
for g in $(find "$HOME/.espressif" -maxdepth 6 -type f -name "$GCC_NAME" 2>/dev/null); do
  b="$(dirname "$g")"
  CROSS="$(dirname "$b")"
  [[ -x "$CROSS/bin/$AR_NAME" ]] && break
  CROSS=""
done
if [[ -z "$CROSS" || ! -x "$CROSS/bin/$GCC_NAME" ]]; then
  echo "error: $GCC_NAME not found under ~/.espressif" >&2
  exit 1
fi
CC="$CROSS/bin/$GCC_NAME"
AR="$CROSS/bin/$AR_NAME"
echo "CC  = $CC"
echo "AR  = $AR"
"$CC" --version | head -1

# ---- libc surface probe ---------------------------------------------------
if [[ "$PROBE" == "1" ]]; then
  echo "== newlib header presence under the cross gcc:"
  for h in poll.h pthread.h malloc.h sched.h spawn.h pwd.h netdb.h stdio_ext.h \
           sys/mman.h sys/epoll.h sys/event.h sys/socket.h sys/wait.h sys/select.h \
           sys/resource.h sys/ioctl.h sys/file.h sys/un.h sys/prctl.h sys/syscall.h \
           execinfo.h dlfcn.h ucontext.h arpa/inet.h netinet/tcp.h sys/prctl.h; do
    printf '  %-18s ' "$h"
    if echo "#include <$h>" | "$CC" -E -x c - >/dev/null 2>&1; then echo yes; else echo NO; fi
  done
  echo "== sigaction/sigaltstack in <signal.h>:"
  printf '  sigaction      '
  if echo "#include <signal.h>
int test(void){struct sigaction sa; sa.sa_flags=0; (void)sa; return sigaction(0,0,0);}" \
      | "$CC" -E -x c - >/dev/null 2>&1 || \
     echo "#include <signal.h>
struct sigaction { int x; };" | "$CC" -fsyntax-only -x c - >/dev/null 2>&1; then
    echo yes
  else
    echo NO
  fi
  exit 0
fi

# ---- flags ----------------------------------------------------------------
# -O2, per-function sections for --gc-sections, same FP contract discipline
# as upstream (common.mk SEC_FLAGS), plus the shim include path FIRST: the
# replacement <signal.h>, <stdio_ext.h>, <sys/resource.h> and the
# socket-family headers shadow newlib's (or supply what it lacks).
#
# Static-BSS sizing for ESP32-C3 (~320 KB usable DRAM after IDF statics):
# the runtime's default static buffers alone are ~880 KB. The macros below
# size them for the target (all are #ifndef-guarded in the vendor tree via
# patches/0040; defaults live in sp_gc.h / sp_alloc.c):
#   sp_gc_roots[SP_GC_STACK_MAX]           65536*8 -> 256 KB, now 4096*8 = 32 KB
#   sp_gc_remembered[SP_GC_REMEMBERED_MAX] 65536*8 -> 256 KB, now 1024*8 =  8 KB
#   sp_gc_pinned[SP_GC_PINNED_MAX]         16384*8 -> 128 KB, now 1024*8 =  8 KB
#   sp_alloc_stats[SP_ALLOC_STATS]          8192*24 -> 192 KB, now 512*24 = 12 KB
#   sp_str_shape[SP_STR_SHAPE_MAX]           8192*8 ->  64 KB, now 512*8 =  4 KB
# Overflow behavior is graceful (heap-allocated extension segments for
# roots; counts saturate for remembered/pinned/stats), see sp_gc.h:62-101.
# SP_GC_STACK_MAX MUST match the app TU build (both sides consult it for
# sp_gc_roots[] and the SP_GC_ROOT bound) -- tools/build-app.sh --target=esp32
# passes the same list via SPINEL_ESP32_CFLAGS.
SHIM="$ROOT/shim/esp32"
VENDOR="$ROOT/vendor/spinel"
OUT="$ROOT/build/rt-$TARGET"
mkdir -p "$OUT"

SP_SZ="-DSP_GC_STACK_MAX=4096 -DSP_GC_REMEMBERED_MAX=1024 -DSP_GC_PINNED_MAX=1024 \
  -DSP_ALLOC_STATS=512 -DSP_STR_SHAPE_MAX=512 \
  -DSP_DYN_SYMS_MAX=256 -DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8"

# Arch flags: riscv32 needs the exact C3 march/mabi; xtensa (windowed ABI,
# soft-float defaults) must get NONE for core selection -- ESP32's windowed
# ABI is the default and -march=esp32 is rejected by this compiler. But
# -mlongcalls IS required for esp32: without it, call8 relocations to newlib
# (memcpy etc.) go out of range at link time ("dangerous relocation: call8:
# call target out of range"); IDF builds everything with it.
ARCH_FLAGS=""
if [[ "$TARGET" == "riscv32" ]]; then
  ARCH_FLAGS="-march=rv32imc_zicsr_zifencei -mabi=ilp32"
elif [[ "$TARGET" == "esp32" ]]; then
  ARCH_FLAGS="-mlongcalls"
fi

CFLAGS_RT="-O2 -ffunction-sections -fdata-sections -ffp-contract=off \
  -Wno-all \
  $ARCH_FLAGS \
  -I$SHIM/include -I$VENDOR/lib -I$VENDOR/lib/regexp -I$VENDOR/lib/regexp/shim \
  -DSP_NO_SIGNALS -D__ESP32_SHIM__ $SP_SZ"

# ---- the source list (upstream Makefile RT_MEMBERS in full: 32 members.
# Nothing is skipped on ESP32 -- sp_net/sp_system/sp_process* compile fine
# against the shims and raise honest runtime ENOSYS errors where the OS has
# no answer; sp_cold.c's Linux-only debuginfo walkers and sp_dtoa.c's x86
# intrinsics are #ifdef'd by the code itself under !__ESP32_SHIM__ probe notes)
RT_MEMBERS="sp_bigint sp_crypto sp_pack sp_time sp_core sp_net sp_system sp_gc sp_slab sp_alloc \
  sp_dtoa sp_marshal sp_format sp_string sp_inspect sp_poly_cold sp_array sp_str \
  sp_str_cold sp_str_crypt sp_hash sp_proc sp_exc sp_re sp_random sp_fiber sp_sched \
  sp_io sp_iobuffer sp_cold sp_process sp_process_status"

RE_SRC="re_compile.c re_exec.c re_utf8.c unicase.c re_spinel.c"

compile() { # compile <src> <obj>
  echo "  CC $1"
  "$CC" $CFLAGS_RT -c "$1" -o "$2"
}

echo "== compiling regexp engine"
for f in $RE_SRC; do compile "$VENDOR/lib/regexp/$f" "$OUT/regexp-${f%.c}.o"; done

echo "== compiling runtime members"
for m in $RT_MEMBERS; do compile "$VENDOR/lib/$m.c" "$OUT/$m.o"; done

echo "== compiling shim"
compile "$SHIM/src/sp_esp32_stub.c" "$OUT/sp_esp32_stub.o"
# Coroutine switch: RV32 uses src/ucontext.S; Xtensa has a windowed register
# ABI and needs its own implementation. ucontext_xtensa.S is NOT YET WRITTEN
# (see docs/plan-xtensa-port.md): no fiber-using app is compiled for esp32, so
# the ucontext symbols are not pulled from this archive. Skip it and warn; the
# archive stays linkable for apps without Fiber.
UCFILE="$SHIM/src/ucontext.S"
[[ "$TARGET" == "esp32" ]] && UCFILE="$SHIM/src/ucontext_xtensa.S"
if [[ -f "$UCFILE" ]]; then
  "$CC" $CFLAGS_RT -x assembler-with-cpp -c "$UCFILE" -o "$OUT/ucontext.o"
else
  echo "  !! $UCFILE missing -- skipping coroutine switch (no esp32 app uses Fiber)"
fi

echo "== archiving"
rm -f "$OUT/libspinel_rt_esp32.a"
"$AR" rcs "$OUT/libspinel_rt_esp32.a" "$OUT"/*.o
echo "OK: $OUT/libspinel_rt_esp32.a"
ls -la "$OUT/libspinel_rt_esp32.a"
