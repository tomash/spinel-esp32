#!/bin/bash
# tools/preset-stress.sh -- prove a capacity preset correct on the host.
#
# Builds a host runtime from $SPINEL_SRC with presets/<preset>.defs (the same
# -D list tools/build-rt.sh gives the ESP32 archive), then runs spinel's GC
# torture tests against it with the matching defines on the program side:
#   - the `# spinel: gc-stress` corpus under SPINEL_GC_STRESS=2 (collect at
#     every allocation), with and without SPINEL_GC_VERIFY;
#   - the `# spinel: gc-minor` corpus under SPINEL_GC_MINOR=1, where tiny
#     remembered/pinned sets overflow constantly and must fall back to a
#     full mark without losing an object.
# A small preset shrinks exactly the tables these tests drive past their
# bounds (roots spilling to the heap segment, set overflow), so a pass here
# is the evidence that the sizes are capacity tuning, not correctness risks.
#
# Usage: SPINEL_SRC=~/spinel-size-floor tools/preset-stress.sh small [-jN]
#        ONLY='fiber_|exception_class' ... (re-check a subset)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${SPINEL_SRC:-$ROOT/vendor/spinel}"
PRESET="${1:-small}"
JOBS="${2:--j2}"; JOBS="${JOBS#-j}"
DEFS="$(grep -v '^[[:space:]]*\(#\|$\)' "$ROOT/presets/$PRESET.defs" | sed 's/^[[:space:]]*/-D/' | tr '\n' ' ')"
WORK="$ROOT/build/preset-stress-$PRESET"
rm -rf "$WORK"; mkdir -p "$WORK/rt" "$WORK/t"
COPT="-O1 -DSP_ONE_STRING -Wno-all -ffunction-sections -fdata-sections -ffp-contract=off"
echo "==> host runtime ($PRESET): $DEFS"
cp "$SRC/lib/libspinel_rt.a" "$WORK/rt/libspinel_rt.a"
members="$(sed -n 's/^RT_MEMBERS = //p' "$SRC/Makefile")"
# shellcheck disable=SC2086
printf '%s\n' $members | xargs -P "$JOBS" -I{} \
  sh -c "cc -c $COPT $DEFS -I'$SRC/lib' -I'$SRC/lib/regexp' '$SRC/lib/{}.c' -o '$WORK/rt/{}.o'" || exit 1
( cd "$WORK/rt" && ar r libspinel_rt.a $(printf '%s.o ' $members) ) || exit 1

run_one() { # run_one <src.rb> <mode>
  local src="$1" mode="$2" bn; bn="$(basename "$src" .rb)-$mode"
  local d="$WORK/t/$bn"; mkdir -p "$d"
  if ! "$SRC/spinel" "$src" -S > "$d/t.c" 2> "$d/cc.out"; then echo "SKIP $bn (spinel refused)"; return 0; fi
  # Bundled packages the program requires (`link <pkg>.o` in the driver's
  # build plan): rebuilt from their .c with the preset, as the runtime is.
  local extra="" o plan pdefs
  plan="$("$SRC/spinel" "$src" --print-build 2>/dev/null)"
  # The threaded runtime (libspinel_rt_mt.a) is not what an MCU links: a
  # program that needs it is out of scope here, not a failure.
  if grep -q '^define -DSP_THREADS$' <<<"$plan"; then echo "SKIP $bn (needs the threaded runtime)"; return 0; fi
  pdefs="$(sed -n 's/^define //p' <<<"$plan" | tr '\n' ' ')"
  for o in $(sed -n 's/^link //p' <<<"$plan"); do
    if [[ -f "${o%.o}.c" ]]; then
      # shellcheck disable=SC2086
      cc -c $COPT $DEFS $pdefs -w -I"$SRC/lib" -I"$SRC/lib/regexp" "${o%.o}.c" -o "$d/$(basename "$o")" 2>>"$d/link.err" \
        || { echo "FAIL $bn (package $(basename "$o"))"; return 1; }
      extra="$extra $d/$(basename "$o")"
    else extra="$extra $o"; fi
  done
  # shellcheck disable=SC2086
  if ! cc $COPT $DEFS $pdefs -w -I"$SRC/lib" -I"$SRC/lib/regexp" "$d/t.c" $extra "$WORK/rt/libspinel_rt.a" \
       -lm -lpthread -o "$d/t" 2> "$d/link.err"; then echo "FAIL $bn (link)"; head -3 "$d/link.err"; return 1; fi
  local envs rc=0
  case "$mode" in
    stress0) envs="SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=0" ;;
    stress1) envs="SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1" ;;
    minor)   envs="SPINEL_GC_MINOR=1 SPINEL_GC_STRESS=1 SPINEL_GC_VERIFY_GEN=1" ;;
  esac
  ( cd "$(dirname "$src")" && env $envs timeout "${STRESS_TIMEOUT:-300}" "$d/t" > "$d/out" 2> "$d/err" ) || rc=$?
  if [[ $rc -ne 0 ]] || ! cmp -s "$d/out" "$src.expected"; then
    echo "FAIL $bn (exit $rc)"; diff "$src.expected" "$d/out" | head -4; head -3 "$d/err"; return 1
  fi
  echo "PASS $bn"
}
export -f run_one; export SRC WORK COPT DEFS

# ONLY=<regex> restricts the run to matching test names (re-checking failures).
{ for f in $(grep -l '^# spinel: gc-stress$' "$SRC"/test/*.rb); do echo "$f stress0"; echo "$f stress1"; done
  for f in $(grep -l '^# spinel: gc-minor$' "$SRC"/test/*.rb); do echo "$f minor"; done
} | grep -E -- "${ONLY:-.}" | xargs -P "$JOBS" -L1 bash -c 'run_one "$0" "$1"' > "$WORK/results.txt" 2>&1
pass=$(grep -c '^PASS' "$WORK/results.txt"); fail=$(grep -c '^FAIL' "$WORK/results.txt"); skip=$(grep -c '^SKIP' "$WORK/results.txt")
grep -A4 '^FAIL' "$WORK/results.txt" | head -60
echo "preset-stress $PRESET: $pass pass, $fail fail, $skip skip  (details: ${WORK#$ROOT/}/results.txt)"
[[ $fail -eq 0 ]]
