/*
 * rt_tasks.c -- pinned C tasks that run beside the Ruby VM (issue #10).
 *
 * The Ruby body is one FreeRTOS task that owns all spinel/GC state; nothing
 * here touches it. A task in this file does a fixed-rate job on its own
 * core and publishes plain ints under a spinlock; Ruby reads them through
 * ffi_func accessors (declared in the app, defined here -- same shape as
 * tdisplay.c).
 *
 * Battery sampler: owns ADC1 for the battery divider, samples it every
 * SP_BATT_PERIOD_MS and keeps a SP_BATT_WINDOW-sample moving average, so
 * the value Ruby shows never carries single-read ADC noise. Pinned to
 * core 1 on dual-core parts (classic ESP32 / T-Display); single-core parts
 * (ESP32-C3 / T-OI Plus) fall back to core 0.
 *
 * Not started at boot: an app that drives ADC1 itself (battery.rb) must not
 * find the unit already claimed. Ruby calls esp32_batt_sampler_start().
 */
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Battery divider channel (2:1, 100k/100k: cell = 2 x pin):
   - Classic ESP32 (T-Display): GPIO34 = ADC1_CH6. No curve-fitting scheme
     on this chip, only line fitting with a default reference.
   - ESP32-C3 (T-OI Plus): GPIO2 = ADC1_CH2 (CH0 is GPIO0, a floating
     Grove pin). Calibrated by the eFuse curve-fitting scheme. */
#if CONFIG_IDF_TARGET_ESP32
  #define SP_BATT_CHANNEL ADC_CHANNEL_6   /* GPIO34, ADC1_CH6 */
#else
  #define SP_BATT_CHANNEL ADC_CHANNEL_2   /* GPIO2,  ADC1_CH2 */
#endif

#define SP_BATT_WINDOW     16
#define SP_BATT_PERIOD_MS  20      /* 2 ticks at CONFIG_FREERTOS_HZ=100 -> 320 ms window */
#define SP_BATT_CORE       ((portNUM_PROCESSORS > 1) ? 1 : 0)
/* Above idle (0) and the Ruby main task (1), well below lwIP (18) and
   esp_wifi (23). */
#define SP_BATT_PRIO       5
/* Internal DRAM is tight (see sdkconfig.defaults). The loop only does an
   adc_oneshot read + calibration and never logs, so a small stack does. */
#define SP_BATT_STACK      2048

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static TaskHandle_t s_task;

/* Published by the sampler, read by Ruby on the other core. */
typedef struct {
    int32_t avg_mv;    /* moving average of calibrated pin mV, -1 until first sample */
    int32_t last_mv;   /* most recent single read */
    int32_t min_mv;    /* raw-read extremes since the last spread read */
    int32_t max_mv;
    int32_t samples;   /* successful reads since start */
    int32_t errors;    /* failed reads since start */
    int32_t core;      /* xPortGetCoreID() as seen by the sampler, -1 until it runs */
} batt_snap_t;

static batt_snap_t s_snap = { .avg_mv = -1, .last_mv = -1, .min_mv = INT32_MAX,
                              .max_mv = INT32_MIN, .core = -1 };
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static int adc_setup(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1, .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) return -1;
    adc_oneshot_chan_cfg_t chan = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
    };
    if (adc_oneshot_config_channel(s_adc, SP_BATT_CHANNEL, &chan) != ESP_OK) return -2;
#if CONFIG_IDF_TARGET_ESP32
    adc_cali_line_fitting_config_t cc = {
        .unit_id = ADC_UNIT_1,
        .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
        .default_vref = 1100, /* mV; used only if eFuse VREF is unburnt */
    };
    adc_cali_create_scheme_line_fitting(&cc, &s_cali); /* optional */
#else
    adc_cali_curve_fitting_config_t cc = {
        .unit_id = ADC_UNIT_1, .chan = SP_BATT_CHANNEL,
        .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
    };
    adc_cali_create_scheme_curve_fitting(&cc, &s_cali); /* optional */
#endif
    return 0;
}

/* Calibrated pin voltage in mV, or negative on failure. */
static int adc_read_mv(void)
{
    int raw = -1, mv = -1;
    if (adc_oneshot_read(s_adc, SP_BATT_CHANNEL, &raw) != ESP_OK) return -2;
    if (!s_cali || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -3;
    return mv;
}

static void batt_sampler(void *arg)
{
    (void)arg;
    int32_t ring[SP_BATT_WINDOW] = { 0 };
    int32_t sum = 0;
    uint32_t n = 0;

    taskENTER_CRITICAL(&s_mux);
    s_snap.core = xPortGetCoreID();
    taskEXIT_CRITICAL(&s_mux);

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        int mv = adc_read_mv();
        if (mv >= 0) {
            int i = n % SP_BATT_WINDOW;
            sum += mv - ring[i];
            ring[i] = mv;
            n++;
            uint32_t cnt = n < SP_BATT_WINDOW ? n : SP_BATT_WINDOW;
            taskENTER_CRITICAL(&s_mux);
            s_snap.avg_mv = sum / (int32_t)cnt;
            s_snap.last_mv = mv;
            if (mv < s_snap.min_mv) s_snap.min_mv = mv;
            if (mv > s_snap.max_mv) s_snap.max_mv = mv;
            s_snap.samples = (int32_t)n;
            taskEXIT_CRITICAL(&s_mux);
        } else {
            taskENTER_CRITICAL(&s_mux);
            s_snap.errors++;
            taskEXIT_CRITICAL(&s_mux);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SP_BATT_PERIOD_MS));
    }
}

/* Claim ADC1 and start the pinned sampler. Idempotent. 0 on success. */
int esp32_batt_sampler_start(void)
{
    if (s_task) return 0;
    if (!s_adc && adc_setup() != 0) return -1;
    if (xTaskCreatePinnedToCore(batt_sampler, "batt_avg", SP_BATT_STACK, NULL,
                                SP_BATT_PRIO, &s_task, SP_BATT_CORE) != pdPASS) {
        s_task = NULL;
        return -2;
    }
    return 0;
}

static int32_t snap_get(const int32_t *field)
{
    taskENTER_CRITICAL(&s_mux);
    int32_t v = *field;
    taskEXIT_CRITICAL(&s_mux);
    return v;
}

/* Moving-average pin mV, or -1 before the first sample. */
int esp32_batt_avg_mv(void)  { return snap_get(&s_snap.avg_mv); }
/* Latest single (unaveraged) read, for comparing noise against the average. */
int esp32_batt_last_mv(void) { return snap_get(&s_snap.last_mv); }
/* Core the sampler reports running on, or -1 if it has not run yet. */
int esp32_batt_core(void)    { return snap_get(&s_snap.core); }
int esp32_batt_samples(void) { return snap_get(&s_snap.samples); }
int esp32_batt_errors(void)  { return snap_get(&s_snap.errors); }

/* Spread (max - min) of the raw reads since the previous call, then reset:
   the ADC noise the moving average hides. -1 if no read landed since. */
int esp32_batt_spread_mv(void)
{
    taskENTER_CRITICAL(&s_mux);
    int32_t lo = s_snap.min_mv, hi = s_snap.max_mv;
    s_snap.min_mv = INT32_MAX;
    s_snap.max_mv = INT32_MIN;
    taskEXIT_CRITICAL(&s_mux);
    return hi >= lo ? (int)(hi - lo) : -1;
}
