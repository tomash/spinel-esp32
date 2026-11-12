#!/bin/bash
# tools/latency.sh -- status-page serve latency, for before/after comparisons
# (issue #10: VM pinned to the APP core, battery sampler on core 1).
#
# Usage: tools/latency.sh <host-or-ip> [requests=200] [--load]
#   --load runs a second, unmeasured request loop alongside to keep the
#          board's network path busy while we measure.
# Prints p50 / p95 / max of curl's time_total in ms, plus failures.
set -uo pipefail
host="${1:?usage: latency.sh <host-or-ip> [requests] [--load]}"
n="${2:-200}"
load=0; [[ "${3:-}" == "--load" || "${2:-}" == "--load" ]] && load=1
[[ "$n" == "--load" ]] && n=200

load_pid=""
if [[ $load -eq 1 ]]; then
  ( while :; do curl -s -o /dev/null -m 5 "http://$host/" ; done ) &
  load_pid=$!
  trap 'kill $load_pid 2>/dev/null' EXIT
fi

times=(); fails=0
for ((i = 0; i < n; i++)); do
  t=$(curl -s -o /dev/null -m 5 -w '%{http_code} %{time_total}' "http://$host/")
  if [[ "${t%% *}" == "200" ]]; then
    times+=("$(awk -v s="${t#* }" 'BEGIN { printf "%.1f", s * 1000 }')")
  else
    fails=$((fails + 1))
  fi
done

if [[ ${#times[@]} -eq 0 ]]; then
  echo "no successful requests ($fails failed)"; exit 1
fi
printf '%s\n' "${times[@]}" | sort -n | awk -v fails="$fails" -v loaded="$load" '
  { v[NR] = $1 }
  END {
    p50 = v[int((NR - 1) * 0.50) + 1]; p95 = v[int((NR - 1) * 0.95) + 1]
    printf "requests %d ok / %d failed%s  p50 %s ms  p95 %s ms  max %s ms\n",
           NR, fails, loaded ? " (under load)" : "", p50, p95, v[NR]
  }'
