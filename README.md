# spinel-esp32

**Ruby → native ESP32 firmware, via [matz/spinel](https://github.com/matz/spinel).**

No interpreter on target. No Python *on target*. No mruby, no picoruby, no mruby-esp32.
Spinel compiles whole Ruby programs through whole-program type inference to plain C,
then to a native binary. This project ports that pipeline to ESP32 (FreeRTOS/ESP-IDF).

## The pipeline

```
app/*.rb ──spinel -S──▶ build/app.c ──┐
                                      ├── cross-cc + libspinel_rt (esp32) ──▶ firmware.elf/bin
libspinel_rt.a (built for riscv32, shimmed for FreeRTOS) ──┘
```

1. Host-side `spinel` emits C for the app (`--cc=<cross-gcc>` makes inference 32-bit-aware).
2. Spinel's runtime (`vendor/spinel/lib/`) is rebuilt for the ESP toolchain against a
   FreeRTOS/newlib compatibility shim (`shim/esp32/`), mirroring the in-tree
   `lib/wasi/` precedent (a constrained target done via header shims).
3. The generated C + runtime + app FFI bindings link into one `.elf`, flash like any
   ESP-IDF app. Ruby code runs as a single FreeRTOS task owning all GC state.

Status: the pipeline runs **on hardware**. Four Ruby apps (battery ADC, GPIO blink,
I2C scan, WiFi status server) compile from Ruby to C to firmware and run on a
LilyGO T-OI Plus (ESP32-C3, RV32IMAC), verified over serial and over the network.

## Prerequisites

A Linux host (tested on Ubuntu 24.04; Debian/Fedora/Arch equivalents work). Two
toolchains live side by side and never touch each other:

1. **The Ruby→C compiler ("spinel")** — built from the vendored source, runs on your
   host, needs nothing but a C compiler and Make.
2. **The ESP32 cross toolchain (ESP-IDF)** — the `riscv32-esp-elf-gcc` compiler and
   headers that compile the generated C (and the runtime) into firmware.

### 1. Host build tools (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential git wget curl ruby
```

`ruby` is needed only for `make deps` (spinel's Makefile uses it to locate the
prism parser sources it downloads from rubygems.org). Any modern system ruby works.

macOS: `xcode-select --install` covers the C toolchain; brew install ruby, wget.

### 2. Build the vendored spinel compiler

```bash
git clone https://github.com/tomash/spinel-esp32.git
cd spinel-esp32
git submodule update --init          # checks out vendor/spinel at the pinned commit
make -C vendor/spinel deps          # fetches prism v1.9.0 + rbs from rubygems (into vendor/)
make -C vendor/spinel               # builds bin/spinel + friends
```

Verify:

```bash
vendor/spinel/bin/spinel --version
# spinel 2026.09.12+8185 (6b6f4602c) [...]
```

### 3. Install ESP-IDF (the cross toolchain)

ESP-IDF brings `riscv32-esp-elf-gcc`, newlib headers, FreeRTOS, and the host-side
Python-based build driver (`idf.py`, esptool). **Python is only ever used on the
host as a build driver** — nothing Python ships on target; the firmware is a
pure C compile of the Ruby program.

```bash
# Ubuntu/Debian prerequisites (esp-idf's official list)
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0

# Clone ESP-IDF v5.5.5 --recursive (shallow submodules break the build)
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5
cd esp-idf-v5.5
./install.sh esp32c3          # installs riscv32 gcc into ~/.espressif (~1.5 GB)
```

Notes:

- Disk: the full clone is ~2 GB, toolchains add ~1.5 GB under `~/.espressif`.
  `/` should have ≥6 GB free before you start.
- To also target classic ESP32 / S3 (xtensa) later, run `./install.sh esp32 esp32s3`.
- The USB serial driver on the T-OI Plus is a CH340 — Linux kernels ≥3.x need no driver.

### 4. Put ESP-IDF in your environment (per shell)

```bash
. ~/esp/esp-idf-v5.5/export.sh
idf.py --version
```

Only needed in shells where you call `idf.py`/esptool directly. The `tools/`
scripts source it themselves.

## Quick start: compile and run a Ruby program (host)

```bash
tools/build-app.sh app/hello.rb              # Ruby -> C -> host binary
./build/hello                                # runs the Ruby program, natively
tools/build-app.sh app/hello.rb --emit-only  # just emit build/hello.c, no compile
```

## Flashing to the T-OI Plus (ESP32-C3)

Connect the board over USB (CH340 serial; `/dev/ttyUSB0`). One-time setup:

```bash
tools/build-rt.sh     # cross-compiles libspinel_rt for riscv32-esp-elf (~7 min, once)
```

Then flash any app with a single command:

```bash
tools/build-app.sh app/blink.rb --target=esp32          # emit C only
tools/build-app.sh app/blink.rb --target=esp32 --flash    # + idf.py build & flash
tools/build-app.sh app/blink.rb --target=esp32 --monitor  # + capture serial output
# serial output lands in build/blink.log
```

`--monitor` resets the board (DTR/RTS sequencing via `tools/monitor.py`) and captures
~12 s of UART output into `build/<app>.log`. Env overrides: `SPINEL_ESP_PORT`
(default `/dev/ttyUSB0`), `SPINEL_ESP_CAPTURE` (seconds).

## Updating firmware over the air (M6)

Once the board is on WiFi, you can update it without a USB cable. Serve the
new app image over the LAN and point the board at it:

```bash
tools/build-app.sh app/wifi_status.rb --target=esp32    # rebuilds firmware
mkdir -p /tmp/pub && cp firmware/build/spinel_esp32_app.bin /tmp/pub/spinel.bin
cd /tmp/pub && python3 -m http.server 8000 &

curl http://spinel-esp32.local/ota/status               # slots + versions
curl "http://<board>/ota/start?url=<host-ip>:8000/spinel.bin&force=1"
curl http://<board>/ota/reboot
```

- `url` is a **bare** `host:port/path` (no scheme). Serve the image from any
  LAN host (e.g. `python3 -m http.server 8000` in the directory holding
  `spinel.bin`); the board does an HTTP **GET** of it over lwIP.
- Without `force=1`, `/ota/start` refuses an image with the same app version
  as the running slot (guard, no flash writes).
- The image streams into the **inactive** slot with sequential erase —
  heap-safe through the whole ~1.27 MB transfer (min ~74.9 KB free).
- After reboot the new slot boots as PENDING_VERIFY; it marks itself valid
  only after successfully serving a request. A bad image is rolled back by
  the bootloader on the next reset automatically.
- Both slots must keep a working image: flash a *known-good* image into the
  second slot first (OTA the clean build onto itself) before experimenting.

## Demo apps (all verified on hardware)

| App | What it proves | Serial output |
|-----|----------------|---------------|
| `app/hello.rb` | pipeline on host | prints from Ruby |
| `app/blink.rb` | GPIO output | "Blink done... GPIO output from Ruby works" |
| `app/battery.rb` | ADC + eFuse calibration | battery cell voltage, calibrated |
| `app/i2c.rb` | I2C master bus | SDA=18/SCL=19 scan, device list |
| `app/wifi_status.rb` | WiFi STA + TCP + HTTP from Ruby | scan, association, served requests |

Details:

- **`blink.rb`** — drives the user LED (GPIO3, active-high) via `esp_rom gpio`
  FFI: configure + 10 on/off cycles with `nanosleep`. Verified: 10 blinks in 4.3 s,
  `main() returned 0`.
- **`battery.rb`** — reads VBAT through the board's 2:1 divider (100k/100k on
  **GPIO2 = ADC1_CH2** on the C3 — not CH0, which is GPIO0 and floats) using
  `adc_oneshot` + **eFuse curve-fitting calibration**
  (`adc_cali_create_scheme_curve_fitting` → `adc_cali_raw_to_voltage`).
  Naive `raw × 3300 / 4095` scaling over-reads by ~11–15% on ESP32-C3 (see
  LilyGO issue #19); the calibrated path reads the actual cell voltage —
  4.05–4.13 V here, matching the multimeter within ~30 mV.
- **`i2c.rb`** — brings up the I2C master bus (SDA=18, SCL=19, 100 kHz) via
  `i2c_master` driver, scans all 112 addresses. T-OI Plus has no populated I2C
  peripherals, so "No I2C devices" is the healthy result.
- **`wifi_status.rb`** — the M4 flagship. All-Ruby WiFi STA + web server:
  `esp_wifi`/`esp_netif` via FFI adapters, spinel's built-in `TCPServer`
  (`sp_net_listen_host` on lwip), and a **polling** loop — one non-blocking
  `accept_nonblock` per tick, no threads, no blocking accept. Serves a status
  page (LED, battery, SSID, RSSI, channel, IP) on `http://<board-ip>/`, with
  `GET /led/toggle` to flip the on-board LED from a browser — state held in
  the Ruby process. On the home AP it associates in ~10 ms and answers curl
  from the LAN.
- **`wifi_status.rb` (M5 additions)** — the app also grows the LAN-facing
  niceties and the survival kit:

  - **mDNS** — `spinel-esp32.local` announced over `_http._tcp` via the
    `espressif/mdns` managed component (FFI: `mdns_init`,
    `mdns_hostname_set`, `mdns_instance_name_set`, `mdns_service_add`).
    `curl http://spinel-esp32.local/` just works from the LAN.
  - **NTP** — wall clock on the status page via SNTP (`esp_sntp_setoperatingmode`
    … `esp_sntp_init`, querying `pool.ntp.org`). See the *gotcha* below —
    `sntp_get_sync_status()` is a one-shot latch in IDF.
  - **GC tuning (`esp32_gc_tune`)** — spinel's RT ships GC thresholds sized
    for desktops (`sp_gc_threshold` 256 KB etc.). On the ESP32-C3's ~16 KB
    DRAM heap, the churn-triggered GC never fires, and the app walks into an
    OOM death spiral → `abort()` → reboot at ~75 s. The fix writes the
    exported RT symbols (both current and `_init` floor values, because
    post-collection retunes clamp to `_init`): `sp_gc_threshold` 8 KB,
    `sp_str_threshold` 4 KB, `sp_str_old_threshold` 8 KB. Result on silicon:
    heap sawtooth — collections every ~850 loops (~25 s), +3.9 KB recovered
    each, min watermark never below ~11 KB in idle.
  - **Socket-error resilience** — any spinel app doing sockets needs to
    rescue **both** the exchange and the `accept` itself: spinel's
    `exception: false` softens only `EAGAIN`. A peer that RSTs a connection
    still sitting in the listen backlog makes lwip's accept fail with
    `ENOTCONN` (observed: uncaught `Errno::ENOTCONN "Socket is not connected
    @ accept"` → `abort()` → reboot, 3/3 reproducible). The app rescues the
    accept and the read/write exchange separately; an RST storm that
    previously rebooted the board 3× now logs `accept error:`/`client
    error:` lines and keeps serving.

## Repo layout

```
app/          Ruby applications (the actual firmware logic)
docs/         findings.md (runtime port research), plan.md (milestones), boards/
firmware/     ESP-IDF project skeleton (CMakeLists, sdkconfig, app.c slot)
shim/esp32/   FreeRTOS/newlib compatibility shim for libspinel_rt (the core work)
patches/      patches carried against vendor/spinel (series-style)
tools/        build pipeline scripts (host compile, cross compile, flash, monitor)
vendor/       git submodules (spinel)
```

The vendored `vendor/spinel` is pinned to a snapshot of `matz/spinel`
(6b6f4602c) and carries 3 ESP32 patches locally (see `patches/`). The
patched commits are also published on the `esp32-shim` branch of the
fork `github.com/tomash/spinel` — the vendored pin is deliberate; don't
rebase it onto upstream master (upstream moves fast: 513 commits since
the snapshot), and `patches/0050-*.patch` preserves the diff regardless.

## Status

See `docs/plan.md`. M0–M6 complete: host pipeline, cross runtime, hardware demos
(GPIO/ADC/I2C), WiFi STA + web server in pure Ruby — all verified on the
T-OI Plus, including the `/led/toggle` endpoint over the LAN and calibrated
battery voltage matching the multimeter. M5 adds mDNS
(`http://spinel-esp32.local/`), NTP wall-clock on the status page, GC
thresholds tuned for the C3's 16 KB DRAM heap, and socket-error resilience —
verified under an RST storm with zero reboots.

M6 (OTA updates) is done end-to-end: `GET /ota/check`, `POST|GET /ota/start`
(streams the new image from a LAN HTTP server into the inactive slot with
sequential erase, heap-safe ~1.27 MB transfer), `GET /ota/status`,
`GET /ota/reboot`, with bootloader rollback safety (the new image is
auto-rolled back unless the running app proves itself by serving a request).
Two real bugs were found and fixed on the way:

- **connect() EIO from unaligned sockaddr** — lwIP's connect guard rejects
  a GC-heap String pointer that isn't 4-byte aligned (ERR_ARG → EIO) before
  any packet is sent. Fixed in `vendor/spinel` `lib/sp_io.c`
  (`sp_sock_connect_nb_sa`): copy the sockaddr through an aligned
  `struct sockaddr_storage` local; also reject oversized lengths with
  EINVAL.
- **OTA image parsing split the header across reads** — the ESP image
  header + first segment can straddle a chunk boundary; magic 0xE9
  checks failed intermittently (magic 20). Fixed in `app/wifi_status.rb`
  by buffering `hsep` (header bytes) and `rest` separately, and closing
  the data socket on all exit paths.

Firmware layout: dual OTA slots 0x20000 / 0x1f0000 (0x1d0000 each),
`firmware/main/sp_ota_adapter.c` wraps esp_ota, partitions in
`firmware/partitions.csv`. Full story in `docs/plan-m6-ota.md`.

## Constraints honored

- Target binary: pure C compile of Ruby source. No Ruby VM, no parser, no interpreter, no Python.
- Host tooling: spinel (C binary) + a C cross-compiler. ESP-IDF's Python-based build
  drivers (idf.py/esptool) are host-side conveniences only — see docs/plan.md M1 decision.

## License

MIT (matches upstream spinel).
