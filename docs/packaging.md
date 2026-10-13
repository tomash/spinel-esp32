# Packaging: making spinel-esp32 easy to pick up

This project has three audiences with very different patience for setup. The
goal is to give each the shortest path to running code, without pretending the
hard parts (a full ESP-IDF toolchain, a from-source compiler) do not exist.

## The friction, measured

| Step | Cost | Notes |
|------|------|-------|
| `git clone` | seconds | the repo (excl. build output) is ~380 MB, mostly the vendored spinel tree once checked out |
| submodule checkout | seconds | **was broken**: `.gitmodules` had `update = none`, so `git submodule update --init` printed "Skipping submodule" and checked out nothing. Fixed here. |
| `make -C vendor/spinel deps` | ~1 min | downloads prism + rbs from rubygems.org |
| `make -C vendor/spinel` | ~5 min | host compiler, `ccache` helps on repeats |
| ESP-IDF install | ~20–40 min, **~8 GB** | ~4 GB clone + ~4 GB `~/.espressif` toolchains; only needed for firmware, not the host pipeline |
| cross runtime (`libspinel_rt_esp32.a`) | ~7 min | 32 runtime members + regexp engine + shim, per target |
| flash | seconds | needs `dialout` group / correct `/dev/ttyUSB*` |

The two changes that remove the most pain for everyone:

1. **A `Makefile` + `tools/bootstrap.sh`** so the entry point is `make setup`
   and `make flash APP=…`, not four scripts with environment variables.
2. **Prebuilt runtime releases**, so nobody re-derives a byte-identical
   `libspinel_rt_esp32.a` on first run.

## Prebuilt release assets

`tools/bootstrap.sh` downloads the cross runtime from a GitHub Release tagged
by the pinned spinel commit:

```
tag:    spinel-rt-<SPINEL_PIN>
assets: libspinel_rt_esp32_riscv32.a   # ESP32-C3
        libspinel_rt_esp32_esp32.a     # classic ESP32 (xtensa)
        SHA256SUMS
```

Cut them with:

```bash
tools/release-assets.sh publish                # both targets
tools/release-assets.sh publish --host-spinel  # also attach the x86-64 linux `spinel`
```

If no release exists for a pin, `bootstrap.sh` falls back to building the
runtime from source (a warning, not an error). `SPINEL_RT_FROM_SOURCE=1`
forces source, `SPINEL_RT_NO_DOWNLOAD=1` forbids downloads.

The host `spinel` binary is a **compiler that runs on the host** — shipping it
is the same convenience upstream's `make dist` tarballs provide, and does not
put any interpreter on the target (the whole point of the port). It is x86-64
Linux only, so it is opt-in (`--host-spinel`).

**When you move the pin:** republish. `bootstrap.sh` derives the asset URL from
`SPINEL_PIN`, so a new pin with no matching release silently falls back to the
7-minute build.

## Tier 2 (proposed): a Docker image

For "I have a board and Ruby and want to hack" the lowest-friction path is a
container with the toolchain baked in:

```bash
docker run --rm -it --device=/dev/ttyUSB0 -v "$PWD:/w" -w /w \
  ghcr.io/tomash/spinel-esp32 make flash APP=app/blink.rb
```

Base it on `espressif/idf:release-v5.5` (the image IDF itself publishes), and
bake in the prebuilt host `spinel` + runtime archives so the container is ready
to flash on first run. Not implemented yet — it is the natural next step once
the release assets exist.

## Tier 1 (proposed): a no-install demo

A static page (GitHub Pages) with prebuilt `.bin` images for each app and
`esptool-js`, so a visitor can flash `blink` from a browser in ~30 seconds with
zero install. Great for a first impression; not for editing Ruby. Not
implemented yet.

## Repro notes

- `firmware/dependencies.lock` is currently gitignored (it pins the
  `espressif/mdns` managed component). Committing it would make firmware builds
  reproducible; worth doing when the build is otherwise stable.
- The toolchain discovery in `tools/build-rt.sh` finds the cross gcc under
  `~/.espressif` by exact binary name, so it survives IDF version bumps without
  hardcoded version directories. `tools/build-app.sh` still pins the
  `esp-14.2.0_*` path for the `--cc=` probe; a follow-up can share the
  discovery.
