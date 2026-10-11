#!/bin/bash
# tools/apply-patches.sh -- apply the carried series against vendor/spinel.
# Idempotent: skips cleanly if the tree is already patched (checks a stamp).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR="$ROOT/vendor/spinel"
STAMP="$VENDOR/.spinel-esp32-patched"

if [[ -f "$STAMP" ]]; then
  echo "vendor/spinel already patched (stamp: $(cat "$STAMP"))"
  exit 0
fi

cd "$VENDOR"
git checkout -- . 2>/dev/null || true   # start clean
applied=0
for p in "$ROOT"/patches/*.patch; do
  [[ -e "$p" ]] || { echo "no patches to apply"; exit 0; }
  echo "==> applying $(basename "$p")"
  git apply --3way "$p"
  applied=$((applied+1))
done
echo "$applied patches applied @ $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$STAMP"
echo "vendor/spinel patched: $applied patch(es)"
