/*
 * net_esp.c -- firmware/main/net.h on ESP-IDF: WiFi station (esp_wifi),
 * mDNS (espressif/mdns), SNTP (esp_sntp), heap figures (heap_caps) and the
 * GC pacing wifi_status needs. Moved out of app/wifi_status.rb's ffi_source
 * so the same Ruby runs on the Pico 2 W (firmware-pico/main/net_pico.c);
 * the functions are unchanged apart from their names (esp32_* -> net_*,
 * mote_*). OTA lives in sp_ota_adapter.c.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "esp_sntp.h"
#include "esp_heap_caps.h"
#include "time.h"
#include "string.h"
#include "stdint.h"
#include "errno.h"

/* ---- WiFi STA (esp_wifi; connect via event loop) ---- */
static int ws_sta_got_ip = 0;
static int ws_retries = 0;

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data) {
    (void)arg; (void)data;
    /* bases arrive here as pointers to the declared event-loop bases */
    esp_event_base_t ip_ev   = IP_EVENT;
    esp_event_base_t wifi_ev = WIFI_EVENT;
    if (base == ip_ev   && id == IP_EVENT_STA_GOT_IP)      ws_sta_got_ip = 1;
    if (base == ip_ev   && id == IP_EVENT_STA_LOST_IP)     ws_sta_got_ip = 0;
    if (base == wifi_ev && id == WIFI_EVENT_STA_DISCONNECTED) {
        ws_sta_got_ip = 0;
        /* assoc-timeout (0x400) and friends: retry a bounded number of
           times -- the documented station pattern (event context). */
        if (ws_retries < 40) { ws_retries++; esp_wifi_connect(); }
    }
}

#if __has_include("wifi_secrets.h")
  #include "wifi_secrets.h"
#else
  #error "missing firmware/main/wifi_secrets.h -- set WIFI_SSID/WIFI_PASSWORD (env or .env) or copy wifi_secrets.h.example"
#endif

const char *net_wifi_ssid(void) { return WIFI_SSID; }

int net_wifi_init(void) {
    const char *ssid = WIFI_SSID, *pass = WIFI_PASSWORD;
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    if (esp_netif_init() != ESP_OK) return -1;
    if (esp_event_loop_create_default() != ESP_OK) return -2;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return -3;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return -4;
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    /* M6: keep the radio awake. Default WIFI_PS_MIN_MODELEM with the
       AP's DTIM 3 / 102.4 ms beacons lets the STA doze ~900 ms; during
       those dozes the board answers nothing (observed 40-300 ms pings,
       whole-probe 100% loss bursts, curl 8 s timeouts) even with the
       heap healthy. WIFI_PS_NONE trades ~30 mA idle for liveness,
       which matters more for OTA. */
    esp_wifi_set_ps(WIFI_PS_NONE);
    /* M4 fix: everything below was debugging-era deviations from the
       stock station example (pmf_cfg capable=false, forced 11BGN
       protocol, forced HT20, WIFI_PS_NONE, max TX power) -- the AP
       silently ignored our assoc requests until buffers were restored
       to stock; keep the config minimal, exactly like stock IDF. */
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return -5;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ws_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ws_event_handler, NULL);
    if (esp_wifi_start() != ESP_OK) return -6;
    return 0;
}

/* Trigger a connect. `connect` itself is async; got_ip arrives via event. */
int net_wifi_connect(void) {
    return esp_wifi_connect() == ESP_OK ? 0 : -1;
}

/* Diagnostic blocking scan: prints every AP's ssid/rssi/channel/authmode.
   Returns count found (authmode names via a small table). */
int net_wifi_scan(void) {
    wifi_scan_config_t sc;
    memset(&sc, 0, sizeof(sc));
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return -1;
    uint16_t n = 0;
    if (esp_wifi_scan_get_ap_num(&n) != ESP_OK) return -2;
    if (n > 32) n = 32;
    wifi_ap_record_t aps[32];
    uint16_t got = n;
    if (esp_wifi_scan_get_ap_records(&got, aps) != ESP_OK) return -3;
    for (int i = 0; i < got; i++) {
        const char *am = "?";
        switch (aps[i].authmode) {
        case WIFI_AUTH_OPEN: am = "OPEN"; break;
        case WIFI_AUTH_WEP: am = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: am = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: am = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: am = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: am = "WPA2-ENT"; break;
        case WIFI_AUTH_WPA3_PSK: am = "WPA3"; break;
        case WIFI_AUTH_WPA2_WPA3_PSK: am = "WPA2/WPA3"; break;
        default: break;
        }
        printf("SCAN: %-24.24s rssi %4d ch %2d %s\n",
               (const char *)aps[i].ssid, (int)aps[i].rssi,
               (int)aps[i].primary, am);
    }
    return (int)got;
}

int net_wifi_connected(void) {
    return ws_sta_got_ip;
}

/* AP info out-param style: fills ssid (n bytes), returns rssi, or -999
   if not associated. channel via *chan_out. */
int net_wifi_ap_info(char *ssid_out, int cap, int *chan_out) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return -999;
    memset(ssid_out, 0, (size_t)cap);
    strncpy(ssid_out, (const char *)ap.ssid, (size_t)(cap - 1));
    /* chan_out points into a Ruby String's bytes (maybe unaligned) */
    int chan = (int)ap.primary;
    if (chan_out) memcpy(chan_out, &chan, sizeof chan);
    return (int)ap.rssi;
}

/* STA IP as text into ipbuf (esp_netif). 0 on success. */
int net_wifi_ip(char *ipbuf, int cap) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return -1;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return -2;
    return snprintf(ipbuf, (size_t)cap, IPSTR, IP2STR(&ip.ip)) < 0 ? -3 : 0;
}

int mote_delay_ms(int ms) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
    return 0;
}

/* Free-running millisecond clock (for the non-blocking OTA download
   loop's idle deadline; wraps after ~49 days -- fine for a soak). */
int mote_millis(void) {
    return (int)(xTaskGetTickCount() * (1000 / configTICK_RATE_HZ));
}

/* ---- M5a: mDNS (managed component espressif/mdns) ---- */
int net_mdns_start(const char *hostname, const char *instance, const char *ip) {
    if (mdns_init() != ESP_OK) return -1;
    if (mdns_hostname_set(hostname) != ESP_OK) return -2;
    if (mdns_instance_name_set(instance) != ESP_OK) return -3;
    /* board=IP txt makes the service discoverable in a browser
       without knowing the address */
    mdns_txt_item_t txt[1];
    txt[0].key = "ip"; txt[0].value = ip;
    if (mdns_service_add(instance, "_http", "_tcp", 80, txt, 1) != ESP_OK)
        return -4;
    return 0;
}

/* ---- M5b: SNTP (in-tree lwip app, esp_sntp.h) ---- */
int net_sntp_start(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    return 0;
}
/* 1 once NTP has synced, 0 before. NOTE: sntp_get_sync_status() is a
   one-shot latch in IDF -- it returns COMPLETED once, then flips the
   status back to RESET (the next sync is CONFIG_LWIP_SNTP_UPDATE_DELAY
   away, an hour by default). Querying it per-request made the page
   show "(not synced)" after the very first hit even though the system
   clock stays correct, so remember the first COMPLETED forever. */
static int ws_time_synced = 0;
int net_sntp_synced(void) {
    if (!ws_time_synced &&
        sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        ws_time_synced = 1;
    }
    return ws_time_synced;
}
/* Wall time via newlib: "YYYY-MM-DD HH:MM:SS" (UTC) into buf, 0 ok */
int net_time_utc(char *buf, int cap) {
    time_t t = time(NULL);
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) return -1;
    return strftime(buf, (size_t)cap, "%Y-%m-%d %H:%M:%S", &tm) == 0 ? -2 : 0;
}

/* ---- M5c diagnostics: internal-DRAM budget (C3 has no PSRAM;
   spinel RT heap, WiFi, mDNS all share this pool) ---- */
int mote_heap_free(void) {
    return (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
int mote_heap_min_free(void) {
    return (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
int mote_heap_largest_block(void) {
    return (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

/* ---- M5c: re-aim the RT's GC triggers at the part's DRAM ----
   Spinel's collector is churn-triggered: it runs after sp_gc_threshold
   (object heap) / sp_str_threshold (string heap) bytes have been
   allocated, both defaulting to 256 KB -- a desktop number. The C3 has
   ~16 KB of free internal DRAM once WiFi + mbedTLS + mDNS are up, so the
   runtime died of malloc exhaustion (sp_oom_die -> "out of memory",
   abort, reboot) at ~75 s -- about 1/16th of the way to its first
   collection. The heartbeat's interpolated puts strings (~260 B each,
   all garbage) are what piles up. The thresholds are exported data
   symbols of the RT archive (and the _init twins are the floors the
   post-collection retunes clamp to), so point them at numbers the part
   can pay before any of that churn starts. A string-triggered
   collection sweeps the object heap too (sp_str_collect_retune ->
   sp_gc_collect), so the object trigger is a backstop, not the pacer. */
extern size_t sp_gc_threshold, sp_gc_threshold_init;
extern size_t sp_str_threshold, sp_str_threshold_init;
extern size_t sp_str_old_threshold, sp_str_old_threshold_init;
int mote_gc_tune(void) {
    sp_gc_threshold       = 8 * 1024;  sp_gc_threshold_init       = 8 * 1024;
    sp_str_threshold      = 4 * 1024;  sp_str_threshold_init      = 4 * 1024;
    sp_str_old_threshold  = 8 * 1024;  sp_str_old_threshold_init  = 8 * 1024;
    return 0;
}

/* ---- M6: explicit collection pacing (the idle heap-starvation fix) ----
   The churn trigger alone cannot pace this app: at idle the only
   garbage is the heartbeat's interpolated puts strings (~260 B/s),
   so a 4 KB string threshold fires a collection just every ~16 s,
   and in between the IDF heap bottoms out near 300 B free -- long
   enough for esp_wifi/lwIP allocations to fail (observed:
   "wifi:m f null" warnings, ARP stops answering pings, accept
   starves, /status connects but never answers). sp_gc_collect_request()
   is the runtime's own GC.start (sp_gc.h:545); in this single-worker
   build (no SP_THREADS) sp_sched.c routes it straight to
   sp_gc_collect() and then runs pending finalizers. Calling it every
   heartbeat bounds the between-collections decay to one heartbeat's
   churn instead of sixteen seconds' worth. */
void sp_gc_collect_request(void);
/* sp_str_sweep() (sp_alloc.h:234, libspinel_rt_esp32.a) is the UNGATED
   string-heap sweep: sp_gc_collect's string hook is installed as
   sp_str_sweep_gated, which skips the sweep entirely while
   sp_str_heap_bytes <= sp_str_threshold (sp_alloc.c sp_str_sweep_begin).
   At idle, live strings stay far below the 4 KB threshold, so the gated
   hook inside sp_gc_collect reclaims ~nothing and the churn trigger
   (sp_str_alloc's bytes>threshold test) stays the only string reaper --
   still every ~16 s. Calling the ungated sp_str_sweep() explicitly (same
   entry GC.start and shutdown use) reaps both string generations every
   heartbeat regardless of threshold; it does not retune, so the churn
   trigger remains as backstop. */
void sp_str_sweep(void);
int mote_gc_step(void) {
    sp_gc_collect_request();
    sp_str_sweep();
    return 0;
}
