# M6: OTA Firmware Updates — Implementation Plan

> **For the implementing agent**: this plan assumes the repo state at commit `c931edc` (M5).
> Read `README.md` and `docs/findings.md` first. Working dir is `~/spinel-esp32`.
> **Never build inside `~/clacky_workspace`** (pCloud FUSE strips exec bits) — build in `~/spinel-esp32`.

## Goal

Device downloads a new firmware image over WiFi, flashes it into the *other* OTA slot,
reboots into it, and self-marks valid. After the one-time re-partition serial reflash,
no USB cable is ever needed again — iterate on Ruby over the air.

## Chosen design: hybrid (Ruby owns protocol, C owns flash)

- **Ruby** (`app/ota_update.rb` or extend `wifi_status.rb`): drives the HTTP GET over
  `TCPSocket` (existing lwip compat), parses headers, decides chunking, verifies image
  magic, triggers reboot.
- **C FFI adapter** (new `firmware/main/sp_ota_adapter.c`, ~80-100 lines): thin wrappers
  around `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` / `esp_ota_get_next_update_partition`
  / `esp_ota_mark_app_valid_cancel_rollback` / `esp_restart`.
- C helper **never** does network I/O; Ruby hands it a byte buffer per chunk.
  One FFI call per chunk keeps all concurrency risk out of C.

## Current state you inherit

- `firmware/partitions.csv`: single `factory` slot (0x10000, 0x2F0000) — **no OTA
  possible**, must change.
- App size at M5: ~1.09 MB.
- `esp-tls` / `mbedtls` already linked (mdns dependency, M5).
- mDNS + SNTP work (M5): device is `spinel-esp32.local`, has wall clock.
- Heap under burst load descends close to zero before emergency GC fires
  (148 B observed during M5 soak). **OTA streaming is burst load** — see GC notes below.
- Flash chip: 4 MB.

---

## Stage 1 — Partition table + one-time serial reflash

New `firmware/partitions.csv`:

```csv
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x006000,
phy_init, data, phy,     0xf000,   0x001000,
otadata,  data, ota,     0x10000,  0x002000,
ota_0,    app,  ota_0,   0x20000,  0x1D0000,
ota_1,    app,  ota_1,   0x1F0000, 0x1D0000,
```

- Two equal 1.8125 MB app slots + 8 KB otadata; fits 4 MB with margin (ends at 0x3C2000).
  1.8 MB slots give lots of headroom over the current 1.09 MB image.
- **This reflash erases the running app** — schedule it, do it, don't leave it half-done.
- Flash the full image (partition table + bootloader + app) via the normal
  `idf.py -D ... flash` path with the **new** table; the first serial flash after the
  table change writes into `ota_0`.
- Verify boot log shows `ota_0` selected, and `esp_ota_get_running_partition()` agrees.
- ⚠️ `nvs` offset (0x9000) is unchanged → WiFi credentials survive the reflash.

## Stage 2 — C FFI adapter (`sp_ota_adapter.c`)

Add to `firmware/main/CMakeLists.txt` sources. Functions (all `int` return, 0 = ok,
negative errno-ish otherwise — keep the FFI convention already used by `sp_app_adapter.c`):

```c
const char *esp32_ota_partition_name(void);        // debug aid
int  esp32_ota_begin(void);                         // wraps esp_ota_begin(next_part, OTA_SIZE_UNKNOWN, &handle)
int  esp32_ota_write(const uint8_t *buf, size_t len);
int  esp32_ota_end(int abort);                      // abort!=0 → esp_ota_abort
int  esp32_ota_running_slot(void);                  // 0 or 1
int  esp32_ota_mark_valid(void);                    // esp_ota_mark_app_valid_cancel_rollback
int  esp32_ota_reboot(void);                        // esp_restart (never returns)
```

- Use `OTA_SIZE_UNKNOWN` — image length comes from HTTP headers, don't trust `Content-Length` blindly.
- Wrap in a small mutex-free single-instance struct: only the Ruby task may call these (D3:
  single-task Ruby, no second task touches Ruby values; same applies to the OTA handle).
- `ota_begin` should print target partition name to stdout for the serial log.

## Stage 3 — Ruby download handler

Either new `app/ota_update.rb` or an `/ota/start?url=...` route in `wifi_status.rb`
(simpler, reuses the existing status server + route dispatch). Recommended: **route in
`wifi_status.rb`**, since it already owns the listening socket, mDNS advertising, and the
rescue-all accept loop from M5.

Flow:

1. `GET /ota/start?url=192.168.1.x:8000/spinel.bin` → Ruby `TCPSocket` connect to the
   local dev machine (served by `python3 -m http.server` over LAN for testing — **no TLS
   needed for dev**; TLS via Ruby would need mbedtls-wrapping in the shim, out of scope).
2. Send raw HTTP/1.0 `GET`, read headers (existing `Net::HTTP`-style parsing is absent —
   hand-roll: read until `\r\n\r\n`, split lines, find `Content-Length`).
3. Verify first bytes of body: `0xE9` magic (esp image) at offset 0 — abort before
   `ota_begin` if wrong.
4. `ota_begin`, then loop: read 4 KB from socket → `ota_write(buf)` → repeat until
   `Content-Length` consumed or EOF.
5. `ota_end` → print progress to stdout → **do not reboot yet**.
6. `GET /ota/status` returns slot info + "ready, reboot pending".
7. `GET /ota/reboot` → `ota_reboot`.

**Memory discipline (critical)**: read the body with a reused `String` buffer (`.clear` +
`<<` or `read` into same object), never accumulate body in Ruby heap. GC pressure under
streaming is the known risk (M5 saw near-zero heap descents under burst). After
`/ota/start` completes, watch `esp32_heap_free` telemetry on `/` — if it sawtooth
crashes the board, raise `sp_str_threshold` in the gc_tune call, or add a periodic
explicit GC every N chunks (`GC.start` if spinel exposes it — check vendor/spinel).

## Stage 4 — Rollback safety

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `firmware/sdkconfig.defaults`.
- On boot, after the status server has **served one successful request** (proof of life),
  call `esp32_ota_mark_valid` once. Track a latch in Ruby (like the M5 SNTP latch) so
  it fires exactly once per boot.
- Test the rollback path deliberately: flash a broken image (e.g. abort in the middle),
  confirm device boots back into the old slot, confirm log shows rollback.

## Stage 5 — verification checklist (all on silicon)

1. Serial reflash with new table; boot into `ota_0`; credentials + mDNS still work.
2. `python3 -m http.server` on LAN; trigger `/ota/start` with the current image
   (same binary) → expect full write + `ota_end` ok + `/ota/status` "pending".
3. `/ota/reboot` → device comes back on `spinel-esp32.local` (mDNS), serves status page.
4. **Reboot-loop soak**: run `/ota/start` → `/ota/reboot` three times in a row; device
   must stay reachable. Watch for rollback-loop (means mark-valid logic is broken).
5. **Bad image test**: truncated image → `ota_end(abort)` path → device stays on old
   image, serves, no crash.
6. **Power-cut test** (optional, dev-board risk accepted per issue #1): pull power mid
   `ota_write`; confirm bootloader falls back cleanly.
7. **Weak-network soak**: throttle the serving side (e.g. `--limit-rate 50k` equivalent)
   while a chunked write happens; confirm TCP window collapse doesn't stall the board
   past the ~10s socket timeout; `SO_RCVTIMEO` on the OTA socket if the shim supports it.

## Effort estimate

- Stage 1: ~30 min (mostly waiting for erase/flash)
- Stage 2: ~2 h (adapter + FFI externs in app.c regenerated + CMake)
- Stage 3: ~3 h (Ruby HTTP handler + streaming + memory discipline + soak)
- Stage 4: ~1 h
- Stage 5: ~1-2 h of soak testing
- **Total: 1-2 focused days.**

## Files touched (expected)

- `firmware/partitions.csv` — new OTA layout
- `firmware/sdkconfig.defaults` — `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- `firmware/main/sp_ota_adapter.c` — NEW
- `firmware/main/CMakeLists.txt` — add source
- `firmware/main/app.c` — regenerated (new FFI externs)
- `app/wifi_status.rb` — OTA routes + mark-valid latch
- `README.md` — M6 section
- `docs/plan.md` — tick M6 (note: plan.md's M4 differs from the README's milestone numbering; README is authoritative for M6)

## Pitfalls (from M5 experience, do not relearn)

- `accept_nonblock(exception: false)` **inside** begin/rescue, not around it —
  spinel softens only EAGAIN; RST storms surface as `ENOTCONN` on accept and must be
  rescued explicitly.
- SNTP is one-shot per boot — latch `esp32_sntp_synced` to avoid re-triggering.
- Multi-line git commits via PTY will fail — write a script to `/tmp` and bash it.
- Serial monitor: use `grep -a` for binary noise.
- Watch `esp32_heap_free`/`esp32_heap_largest_block` telemetry during every soak —
  internal DRAM starvation is the recurring failure mode of this project.
---

## OUTCOME (2026-10-12) — M6 complete, verified end-to-end twice

All five stages shipped. Final round trip on the clean image (app 1,269,296 B, md5 08d0767d, ELF d9c6f7d3d, running 0.7.0):

- `GET /ota/check` reports both slots + versions.
- Guard path: same-version image refused with a clear error (`version guard refused image (rc -2)`), no flash writes.
- `force=1` round trip: streamed 1,269,296 B from LAN HTTP server into the inactive slot, sequential erase, `image sealed, boot slot ota_1 on next reboot`, rebooted into 0x1f0000, ELF matches, HTTP 200 proof-of-life, `marked valid (rollback cancelled)`. Ran twice (instrumented build then clean build), plus one more forced OTA back to ota_0.
- Heap telemetry through the whole transfer: min ~74.9 KB free, largest block 64 KB intact — no fragmentation drift, no leak.

## Real bugs found (worth remembering)

1. **lwIP rejects unaligned sockaddrs.** `connect(sockaddr)` with a String from the GC heap fails with EIO (ERR_ARG via the IS_SOCK_ADDR_ALIGNED guard) before any packet leaves. `bind()` dodged it because the resolver's addrinfo buffer is aligned. Fix: vendor/spinel `lib/sp_io.c` `sp_sock_connect_nb_sa` copies through `struct sockaddr_storage` and rejects salen > sizeof(sockaddr_storage) with EINVAL. Diagnosis cost the bulk of M6; the EIO was blamed on heap pressure for two milestones.
2. **OTA header split across reads.** The image header + segment headers can straddle a download-chunk boundary; parsing assumed the first chunk holds a whole header, so magic checks intermittently saw 20 (0x14 from segment data) instead of 0xE9. Fix in `app/wifi_status.rb`: hold header bytes in `hsep` until the full ESP image header (8 B + segment count * 20 B) is assembled, then feed the remainder (`rest`) through the streaming writer. Same bug class bites any "peek prefix then stream" protocol.
3. **Bare `idf.py build` does not regenerate `firmware/main/app.c`.** The Ruby→C step only runs inside `tools/build-app.sh`; rebuilding with idf.py alone silently flashes stale Ruby. And on this board `idf.py flash` without `-p` auto-detects /dev/ttyUSB1 (CP2104 carrier port, not the C3) and fails with "This chip is ESP32, not ESP32-C3" — always pass `-p /dev/ttyUSB0`.
4. **Instrumentation tax**: the [lwip] printf + /dev/connect fault injector were removed after diagnosis; instrumented app was 1,299,632 B vs 1,269,296 B clean (30 KB), and stripping restored boot heap headroom.

## Rollback safety behavior (shipped)

- New slot boots once as PENDING_VERIFY; the Ruby app calls `esp32_ota_mark_valid()` only after one successful HTTP serve.
- Never mark valid while an OTA write is in progress or on boot before first serve — a bad image rolls back on next reset automatically.
