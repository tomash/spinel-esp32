#!/bin/bash
# tools/release-assets.sh -- build + publish the prebuilt artifacts for a pin.
#
# The release is keyed by the spinel pin in SPINEL_PIN, so tools/bootstrap.sh
# can fetch exactly the runtime that matches the vendored source:
#
#   tag:  spinel-rt-<pin>
#   assets:
#     libspinel_rt_esp32_riscv32.a   (ESP32-C3)
#     libspinel_rt_esp32_esp32.a     (classic ESP32 / S3-adjacent xtensa)
#     spinel                         (host compiler; optional, see below)
#
# Usage:
#   tools/release-assets.sh build         # build the assets into dist/
#   tools/release-assets.sh publish       # build + create/update the release
#   tools/release-assets.sh publish --host-spinel   # also attach the host binary
#
# Notes:
#   * Building the runtime needs ESP-IDF (ESP_IDF=... overrides the default).
#   * The host `spinel` binary is x86-64 Linux, dynamically linked against
#     libc only (no interpreter/VM on target -- it is a host-side compiler).
#     Shipping it is a convenience for people who do not want the ~5 min host
#     build; upstream ships source tarballs from `make dist` for the same
#     reason. Do not attach it unless you accept it is host-arch specific.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PIN="$(tr -d '[:space:]' < SPINEL_PIN)"
TAG="spinel-rt-$PIN"
DIST="$ROOT/dist"
REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || echo tomash/spinel-esp32)"

cmd="${1:-build}"
shift || true
WANT_HOST=0
for a in "$@"; do
  case "$a" in
    --repo=*) REPO="${a#--repo=}" ;;
    --host-spinel) WANT_HOST=1 ;;
    *) echo "unknown arg: $a" >&2; exit 2 ;;
  esac
done

build_rt() { # <target> <archive-out-name>
  local target="$1" out="$2"
  echo "==> building runtime: $target"
  tools/build-rt.sh "$target"
  cp "build/rt-$target/libspinel_rt_esp32.a" "$DIST/$out"
  echo "  $out  ($(wc -c < "$DIST/$out") bytes)"
}

mkdir -p "$DIST"

case "$cmd" in
  build)
    build_rt riscv32 libspinel_rt_esp32_riscv32.a
    build_rt esp32   libspinel_rt_esp32_esp32.a
    if [ "$WANT_HOST" = "1" ]; then
      test -x vendor/spinel/bin/spinel || { echo "host spinel not built" >&2; exit 1; }
      cp vendor/spinel/bin/spinel "$DIST/spinel"
      echo "  spinel  ($(wc -c < "$DIST/spinel") bytes)"
    fi
    ( cd "$DIST" && sha256sum ./*.a ./*[^.] 2>/dev/null > SHA256SUMS || true )
    echo "==> assets in $DIST"
    ls -l "$DIST"
    ;;
  publish)
    "$0" build $([ "$WANT_HOST" = "1" ] && echo --host-spinel)
    if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
      echo "==> release $TAG exists, uploading assets"
      gh release upload "$TAG" "$DIST"/* --repo "$REPO" --clobber
    else
      echo "==> creating release $TAG"
      gh release create "$TAG" "$DIST"/* --repo "$REPO" \
        --title "spinel runtime for pin ${PIN:0:9}" \
        --notes "Prebuilt libspinel_rt_esp32.a for SPINEL_PIN \`$PIN\`.

Downloaded automatically by \`make setup\` (tools/bootstrap.sh). Built from
vendor/spinel at the pinned commit against ESP-IDF v5.5.5.

- libspinel_rt_esp32_riscv32.a -- ESP32-C3 (riscv32, rv32imc_zicsr_zifencei)
- libspinel_rt_esp32_esp32.a   -- classic ESP32 (xtensa, windowed ABI)"
    fi
    echo "==> https://github.com/$REPO/releases/tag/$TAG"
    ;;
  *)
    echo "usage: tools/release-assets.sh [build|publish] [--host-spinel] [--repo=OWNER/REPO]" >&2
    exit 2
    ;;
esac
