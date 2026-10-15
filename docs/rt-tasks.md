# Pinned C tasks beside the Ruby VM (issue #10)

The Ruby program is one FreeRTOS task that owns all spinel state: the heap,
the GC, every object. That stays true. What this adds is a place for
**plain C tasks** that do fixed-rate work on their own core and hand Ruby
nothing but integers. This uses the classic ESP32's second core without
touching spinel's threading model (`SP_THREADS` stays off).

Code: `firmware/main/rt_tasks.c`. First user: `app/wifi_status.rb`.

## Why not Ruby `Thread`s on the other core

Running Ruby on two cores would need `SP_THREADS` plus a thread-safe
scheduler and GC: objects shared across threads, stop-the-world collection,
and rules for the shared heap. That is a runtime project. A pinned C task
gets real parallelism today. We revisit Ruby-level concurrency only if a
job turns up that C tasks can't do.

## Who runs where

| Task | Classic ESP32 (T-Display) | ESP32-C3 (T-OI Plus) | Priority |
|------|---------------------------|----------------------|----------|
| esp_wifi | core 0 (IDF default pin) | core 0 | 23 |
| lwIP `tcpip` | unpinned | core 0 | 18 |
| battery sampler (`batt_avg`) | **core 1** | core 0 | 5 |
| Ruby VM (IDF main task) | **core 1** | core 0 | 1 |
| idle | both cores | core 0 | 0 |

- **Ruby VM on the APP core:** `firmware/sdkconfig.defaults.esp32` sets
  `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y`. IDF reads that file only when
  `IDF_TARGET=esp32`, so the C3 build is unchanged. With WiFi on core 0,
  the Ruby loop and the network stack no longer compete for one CPU.
  `sp_app_adapter.c` logs the core at boot:
  `spinel app_main: calling generated main() on core 1`.
- **Sampler:** pinned with `xTaskCreatePinnedToCore(..., SP_BATT_CORE)`,
  where `SP_BATT_CORE` is `(portNUM_PROCESSORS > 1) ? 1 : 0`. On a
  single-core part it falls back to core 0. (Pinning to core 1 there would
  trip an assert, so this check is required, not just tidiness.)
- **Sampler and VM share core 1:** the sampler has the higher priority, so
  it preempts the Ruby loop. It wakes every 20 ms for one ADC read plus
  calibration, a few tens of µs. That is noise for the VM, and it keeps
  the sampler's timing steady however busy Ruby gets.

Sticky sdkconfig: an already-configured `firmware/sdkconfig` keeps its old
affinity. `tools/build-app.sh` runs `idf.py set-target`, which regenerates
the file. If you drive `idf.py` by hand, delete `firmware/sdkconfig` once.

## Battery sampler

| Parameter | Value | Why |
|-----------|-------|-----|
| Channel | ESP32: GPIO34 = ADC1_CH6 · C3: GPIO2 = ADC1_CH2 | Battery divider node, 2:1 (cell = 2 × pin) |
| Calibration | ESP32: line fitting · C3: eFuse curve fitting | Each chip's only or best scheme |
| Period | 20 ms (`vTaskDelayUntil`) | 2 ticks at `CONFIG_FREERTOS_HZ=100` |
| Window | 16 samples, running sum | 320 ms of smoothing; O(1) per sample |
| Stack | 2048 B | Internal DRAM is tight; the loop never logs |

The sampler **owns ADC1**. `adc_oneshot_new_unit` lets only one owner
claim a unit, so apps that use the sampler must not open ADC1 themselves.
For the same reason it is **not started at boot**: `app/battery.rb` drives
ADC1 directly and must find it free. Apps opt in with
`esp32_batt_sampler_start()`.

### Ruby API

Declared with `ffi_func` in the app and defined in `rt_tasks.c`, the same
way as the `TDisplay` module and `tdisplay.c`:

```ruby
module ESP32
  ffi_func :esp32_batt_sampler_start, [], :int  # claim ADC1, start task; idempotent; 0 = ok
  ffi_func :esp32_batt_avg_mv,        [], :int  # moving average, pin mV; -1 before first sample
  ffi_func :esp32_batt_last_mv,       [], :int  # latest single read, pin mV
  ffi_func :esp32_batt_spread_mv,     [], :int  # max-min of raw reads since last call (resets); -1 if none
  ffi_func :esp32_batt_samples,       [], :int  # successful reads since start
  ffi_func :esp32_batt_core,          [], :int  # xPortGetCoreID() seen by the task; -1 before it runs
end

cell_v = ESP32.esp32_batt_avg_mv * 2 / 1000.0   # 2:1 divider
```

`rt_tasks.c` also defines `esp32_batt_errors()` (failed reads), but
`wifi_status.rb` doesn't declare it.

### Sharing rules (keep these for any future task)

- The task keeps its working state private: here, the ring buffer and the
  running sum.
- It publishes a small snapshot struct under a `portMUX` spinlock
  (`taskENTER_CRITICAL`). That works across cores on both Xtensa and
  RISC-V, and the critical sections are a few loads and stores.
- Ruby gets copies of plain ints. No pointers into the task's memory, and
  no spinel objects created or touched from the task: the GC is
  single-threaded and doesn't know the task exists.
- The core id comes from inside the task (`xPortGetCoreID()`), not from
  the pinning constant, so the logs show where the task actually ran.

## What `wifi_status.rb` shows

- The LCD footer, the status page and the boot screen all show the
  **average**, never a single read.
- The once-per-second heartbeat line carries the raw comparison (values
  below are illustrative, not measured):

  ```
  [1200] idle; cell 4.12V batt avg 2061 last 2049 spread 38 n 1517 core 1 heap ...
  ```

  `avg`, `last` and `spread` are pin mV (`spread` covers the last ~50
  samples). `n` should climb by about 50 per heartbeat. `core` is the
  sampler's own `xPortGetCoreID()`.

## Measuring the effect

Compare three builds: (A) `main` before #10, (B) only the
`sdkconfig.defaults.esp32` change (Ruby VM on core 1), (C) this branch. Use
the same board, the same AP and a steady supply.

1. **ADC noise:** capture serial for about 2 minutes (`tools/monitor.py`
   with `SPINEL_ESP_CAPTURE=120`). For (A), the spread of the displayed
   values; for (C), `spread` against the variation of `avg` across
   heartbeats.
2. **Serve latency:** `tools/latency.sh <board-ip> 200`, then again with
   `--load` (a second, unmeasured request loop). It reports p50, p95, max
   and failures.
3. **Heap jitter:** min and max of `heap` / `min` / `largest` over the same
   capture, idle and during the latency runs. The sampler's stack is the
   only fixed cost (about 2 KB of internal DRAM plus its TCB).

## Adding another task

Put it in `rt_tasks.c` (or a sibling file listed in
`firmware/main/CMakeLists.txt`) and follow the same pattern: started by an
explicit `esp32_*_start()` FFI call, pinned with the
`portNUM_PROCESSORS` fallback, priority between 1 (Ruby) and 18 (lwIP),
results published under a spinlock, and accessors that return ints.
