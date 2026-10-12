#!/bin/bash
# tools/build-rt.sh -- cross-compile libspinel_rt.a for ESP32 (riscv32 first).
#
# Mirrors the upstream Makefile's archive rule (ar rcs over RT_MEMBERS +
# regexp + platform objects) but with the ESP-IDF cross gcc and the
# shim/esp32 headers first on the include path. No upstream files are
# modified: the shim is -I'd in, and POSIX surface newlib lacks is defined
# to fail (the lib/wasi precedent).
#
# Usage:
#   tools/build-rt.sh                # riscv32 (esp32c3), needs IDF_PATH
#   ESP_IDF=/path/to/esp-idf tools/build-rt.sh
#   tools/build-rt.sh --probe        # just check what the cross libc has
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDF="${ESP_IDF:-$HOME/esp/esp-idf-v5.5}"

# ---- toolchain discovery -------------------------------------------------
if [[ ! -d "$IDF" ]]; then
  echo "error: ESP-IDF not found at $IDF (set ESP_IDF=...)" >&2
  exit 1
fi
# ESP-IDF's installer has used two layouts: ~/.espressif/tools/<name>/<ver>/<name>/
# (v5.5 install.sh) and ~/.espressif/frameworks/<name>-<ver>/ (older/idf-env).
# Search for the gcc binary itself so category dirs (e.g. tools/riscv32-esp-elf/)
# can't shadow the real prefix.
CROSS=""
for g in $(find "$HOME/.espressif" -maxdepth 6 -type f -name riscv32-esp-elf-gcc 2>/dev/null); do
  b="$(dirname "$g")"
  CROSS="$(dirname "$b")"
  [[ -x "$CROSS/bin/riscv32-esp-elf-ar" ]] && break
  CROSS=""
done
if [[ -z "$CROSS" || ! -x "$CROSS/bin/riscv32-esp-elf-gcc" ]]; then
  echo "error: riscv32-esp-elf-gcc not found under ~/.espressif" >&2
  exit 1
fi
CC="$CROSS/bin/riscv32-esp-elf-gcc"
AR="$CROSS/bin/riscv32-esp-elf-ar"
echo "CC  = $CC"
echo "AR  = $AR"
"$CC" --version | head -1

# ---- libc surface probe ---------------------------------------------------
if [[ "${1:-}" == "--probe" ]]; then
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
OUT="$ROOT/build/rt-riscv32"
mkdir -p "$OUT"

SP_SZ="-DSP_GC_STACK_MAX=4096 -DSP_GC_REMEMBERED_MAX=1024 -DSP_GC_PINNED_MAX=1024 \
  -DSP_ALLOC_STATS=512 -DSP_STR_SHAPE_MAX=512 \
  -DSP_DYN_SYMS_MAX=256 -DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8"

CFLAGS_RT="-O2 -ffunction-sections -fdata-sections -ffp-contract=off \
  -Wno-all \
  -march=rv32imc_zicsr_zifencei -mabi=ilp32 \
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
"$CC" $CFLAGS_RT -x assembler-with-cpp -c "$SHIM/src/ucontext.S" -o "$OUT/ucontext.o"

echo "== archiving"
rm -f "$OUT/libspinel_rt_esp32.a"
"$AR" rcs "$OUT/libspinel_rt_esp32.a" "$OUT"/*.o
echo "OK: $OUT/libspinel_rt_esp32.a"
ls -la "$OUT/libspinel_rt_esp32.a"
