# app/wifi_status.rb -- M4: WiFi STA + polling web server, all from Ruby.
#
# North star: connect the T-OI Plus to the home AP, then serve a status
# page (LED state, battery cell voltage, WiFi details) over HTTP -- a
# polling architecture, no blocking accept loop and no threads: one
# main loop waits on poll(2) with a small timeout and serves at most
# one request per tick, while also polling the ADC and the WiFi state.
#
# M4b: the index page links to /led/toggle, which flips the on-board
# LED (GPIO3) and re-serves the index. LED state lives in this process;
# the idle heartbeat blink is gone -- the endpoint owns the LED now.
#
# Two halves:
#   * WiFi + ADC via the FFI DSL (same module shape as blink.rb /
#     battery.rb): esp_wifi STA bring-up, esp_wifi_sta_get_ap_info for
#     RSSI/channel/SSID, adc_oneshot for the battery divider.
#   * sockets via spinel's built-in TCPServer (#2922): `require "socket"`
#     is a native no-op that gates the class typing; TCPServer.new maps
#     to sp_net_listen_host, accept_nonblock(exception: false) to
#     sp_sock_accept_nb -> sp_net_accept_nb, which returns nil (well,
#     :wait_readable) when no peer is waiting. All of those symbols are
#     already in our cross-compiled RT archive.

require "socket"

# WiFi credentials are not in this file: they come from
# firmware/main/wifi_secrets.h (gitignored), written by tools/build-app.sh
# from WIFI_SSID / WIFI_PASSWORD (env or .env). ENV.fetch would not work
# here -- spinel compiles it to a getenv() on the device, which has no
# environment, so the fallback literal always won and ended up in app.c.
PORT     = 80
LED_PIN  = 3
DIVIDER  = 2 # 100k/100k divider: cell = 2x pin voltage

# Board identity drives the page header and the mDNS hostname: two boards
# on one LAN must not both claim spinel-esp32.local (mDNS is first-come:
# the second board to start either loses the name or knocks the first off
# it, depending on the responder). T-OI Plus keeps the plain name; the
# T-Display gets an explicit -tdisplay suffix.
def board_name
  ESP32.esp32_board_name.delete("\0")
end

def mdns_host
  if board_name.include?("T-Display")
    "spinel-esp32-tdisplay"
  else
    "spinel-esp32"
  end
end

module ESP32
  ffi_source <<~C
    #include "esp_wifi.h"
    #include "esp_event.h"
    #include "esp_netif.h"
    #include "nvs_flash.h"
    #include "esp_adc/adc_oneshot.h"
    #include "esp_adc/adc_cali.h"
    #include "mdns.h"
    #include "esp_sntp.h"
    #include "esp_heap_caps.h"
    #include "time.h"
    #include "string.h"
    #include "stdint.h"
    #include "errno.h"

    /* ---- battery ADC. Channel and calibration scheme differ per target:
       - ESP32-C3: GPIO2 = ADC1_CH2 (NOT CH0; CH0 is GPIO0, a floating Grove
         pin -- the M4 under-read bug: CH0 drifted ~1.1-1.3V while the real
         divider output sat on CH2). Calibrated by the per-chip eFuse
         curve-fitting scheme.
       - Classic ESP32 (T-Display): battery divider is GPIO36 = ADC1_CH0, and
         the chip has no curve-fitting scheme -- only line fitting with a
         default reference. The Ruby-side pin constant is board-specific;
         this C only needs to compile and read correctly on both.
       Divider 2:1 (100k/100k), same as battery.rb ---- */
    #if CONFIG_IDF_TARGET_ESP32
      #define WS_ADC_CHANNEL ADC_CHANNEL_0   /* GPIO36, ADC1_CH0 */
    #else
      #define WS_ADC_CHANNEL ADC_CHANNEL_2   /* GPIO2,  ADC1_CH2 */
    #endif
    static adc_oneshot_unit_handle_t ws_adc_unit = NULL;
    static adc_cali_handle_t ws_adc_cali = NULL;
    static int ws_adc_inited = 0;

    int esp32_adc_init(void) {
        if (ws_adc_inited) return 0;
        adc_oneshot_unit_init_cfg_t unit = {
            .unit_id = ADC_UNIT_1, .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        if (adc_oneshot_new_unit(&unit, &ws_adc_unit) != ESP_OK) return -1;
        adc_oneshot_chan_cfg_t chan = {
            .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
        };
        if (adc_oneshot_config_channel(ws_adc_unit, WS_ADC_CHANNEL, &chan) != ESP_OK)
            return -2;
    #if CONFIG_IDF_TARGET_ESP32
        adc_cali_line_fitting_config_t cc = {
            .unit_id = ADC_UNIT_1,
            .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
            .default_vref = 1100, /* mV; used only if eFuse VREF is unburnt */
        };
        adc_cali_create_scheme_line_fitting(&cc, &ws_adc_cali); /* optional */
    #else
        adc_cali_curve_fitting_config_t cc = {
            .unit_id = ADC_UNIT_1, .chan = WS_ADC_CHANNEL,
            .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
        };
        adc_cali_create_scheme_curve_fitting(&cc, &ws_adc_cali); /* optional */
    #endif
        ws_adc_inited = 1;
        return 0;
    }

    /* Calibrated battery pin voltage in mV, or negative on failure. */
    int esp32_adc_read_mv(void) {
        if (!ws_adc_inited) return -1;
        int raw = -1;
        if (adc_oneshot_read(ws_adc_unit, WS_ADC_CHANNEL, &raw) != ESP_OK) return -2;
        if (ws_adc_cali) {
            int mv = -1;
            if (adc_cali_raw_to_voltage(ws_adc_cali, raw, &mv) == ESP_OK) return mv;
        }
        return -3; /* cali unavailable */
    }

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

    const char *esp32_wifi_ssid(void) { return WIFI_SSID; }

    int esp32_wifi_init(void) {
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
    int esp32_wifi_connect(void) {
        return esp_wifi_connect() == ESP_OK ? 0 : -1;
    }

    /* Diagnostic blocking scan: prints every AP's ssid/rssi/channel/authmode.
       Returns count found (authmode names via a small table). */
    int esp32_wifi_scan(void) {
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
            printf("SCAN: %-24.24s rssi %4d ch %2d %s\\n",
                   (const char *)aps[i].ssid, (int)aps[i].rssi,
                   (int)aps[i].primary, am);
        }
        return (int)got;
    }

    int esp32_wifi_connected(void) {
        return ws_sta_got_ip;
    }

    /* AP info out-param style: fills ssid (n bytes), returns rssi, or -999
       if not associated. channel via *chan_out. */
    int esp32_wifi_ap_info(char *ssid_out, int cap, int *chan_out) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return -999;
        memset(ssid_out, 0, (size_t)cap);
        strncpy(ssid_out, (const char *)ap.ssid, (size_t)(cap - 1));
        if (chan_out) *chan_out = (int)ap.primary;
        return (int)ap.rssi;
    }

    /* STA IP as text into ipbuf (esp_netif). 0 on success. */
    int esp32_wifi_ip(char *ipbuf, int cap) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif) return -1;
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return -2;
        return snprintf(ipbuf, (size_t)cap, IPSTR, IP2STR(&ip.ip)) < 0 ? -3 : 0;
    }

    int esp32_delay_ms(int ms) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
        return 0;
    }

    /* Free-running millisecond clock (for the non-blocking OTA download
       loop's idle deadline; wraps after ~49 days -- fine for a soak). */
    int esp32_millis(void) {
        return (int)(xTaskGetTickCount() * (1000 / configTICK_RATE_HZ));
    }

    /* ---- M5a: mDNS (managed component espressif/mdns) ---- */
    int esp32_mdns_start(const char *hostname, const char *instance, const char *ip) {
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
    int esp32_sntp_start(void) {
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
    int esp32_sntp_synced(void) {
        if (!ws_time_synced &&
            sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            ws_time_synced = 1;
        }
        return ws_time_synced;
    }
    /* Wall time via newlib: "YYYY-MM-DD HH:MM:SS" (UTC) into buf, 0 ok */
    int esp32_time_utc(char *buf, int cap) {
        time_t t = time(NULL);
        struct tm tm;
        if (gmtime_r(&t, &tm) == NULL) return -1;
        return strftime(buf, (size_t)cap, "%Y-%m-%d %H:%M:%S", &tm) == 0 ? -2 : 0;
    }

    /* ---- M5c diagnostics: internal-DRAM budget (C3 has no PSRAM;
       spinel RT heap, WiFi, mDNS all share this pool) ---- */
    int esp32_heap_free(void) {
        return (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    int esp32_heap_min_free(void) {
        return (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    int esp32_heap_largest_block(void) {
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
    int esp32_gc_tune(void) {
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
    int esp32_gc_step(void) {
        sp_gc_collect_request();
        sp_str_sweep();
        return 0;
    }

    /* ---- M6: OTA flash-write adapters (sp_ota_adapter.c) ----
       Ruby owns the whole download protocol; C only owns the flash
       machinery. esp32_ota_write takes the chunk buffer as binstr: the
       bridge hands the C side the String's own bytes plus the exact
       length published in sp_ffi_bin_len (the read just before it set
       that global for its :binstr answer), so NUL bytes inside a 4 KB
       chunk survive -- strlen would truncate the image at its first
       0x00 byte. The chunk String is the out-buffer of readpartial's
       (n, buf) form: each refill reuses the same allocation. */
    const char *esp32_ota_partition_name(void);
    int esp32_ota_begin(void);
    int esp32_ota_check_version(const uint8_t *buf, size_t len);
    int esp32_ota_write(const uint8_t *buf, size_t len);
    int esp32_ota_end(int abort);
    int esp32_ota_running_slot(void);
    int esp32_ota_mark_valid(void);
    int esp32_ota_reboot(void);
  C

  # LED (kept for reuse across demos)
  ffi_source <<~C
    #include "driver/gpio.h"
    int esp32_led_init(int pin) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        return gpio_config(&cfg);
    }
    int esp32_led_write(int pin, int level) {
        return gpio_set_level(pin, level ? 1 : 0);
    }
  C

  # Board identity: the RT archive is target-agnostic, but the app adapter
  # knows which chip it was compiled for (same CONFIG_IDF_TARGET_ESP32
  # guard as the ADC block above). The name goes on the status page and in
  # the mDNS hostname, so a T-OI Plus and a T-Display on one LAN announce
  # distinct hosts instead of fighting over spinel-esp32.local.
  ffi_source <<~C
    const char *esp32_board_name(void) {
    #if CONFIG_IDF_TARGET_ESP32
        return "TTGO T-Display";
    #else
        return "T-OI Plus (ESP32-C3)";
    #endif
    }
    int esp32_board_has_lcd(void) {
    #if CONFIG_IDF_TARGET_ESP32
        return 1;   /* ST7789 panel, driver in firmware/main/tdisplay.c */
    #else
        return 0;
    #endif
    }
  C

  ffi_func :esp32_adc_init,     [], :int
  ffi_func :esp32_adc_read_mv, [], :int
  ffi_func :esp32_led_init,     [:int], :int
  ffi_func :esp32_led_write,    [:int, :int], :int
  ffi_func :esp32_board_name,   [], :str
  ffi_func :esp32_board_has_lcd, [], :int
  ffi_func :esp32_wifi_ssid,     [], :str
  ffi_func :esp32_wifi_init,     [], :int
  ffi_func :esp32_wifi_connect, [], :int
  ffi_func :esp32_wifi_scan,    [], :int
  ffi_func :esp32_wifi_connected, [], :int
  ffi_func :esp32_wifi_ap_info,  [:ptr, :int, :ptr], :int
  ffi_func :esp32_wifi_ip,       [:ptr, :int], :int
  ffi_func :esp32_delay_ms,      [:int], :int, blocking: true
  ffi_func :esp32_millis,        [], :int
  ffi_func :esp32_mdns_start,    [:str, :str, :str], :int
  ffi_func :esp32_sntp_start,    [], :int
  ffi_func :esp32_sntp_synced,   [], :int
  ffi_func :esp32_time_utc,      [:ptr, :int], :int
  ffi_func :esp32_heap_free,     [], :int
  ffi_func :esp32_heap_min_free, [], :int
  ffi_func :esp32_heap_largest_block, [], :int
  ffi_func :esp32_gc_tune, [], :int
  ffi_func :esp32_gc_step, [], :int
  # M6: OTA flash machinery (C side owns esp_ota_*; Ruby owns the protocol)
  ffi_func :esp32_ota_partition_name, [], :str
  ffi_func :esp32_ota_begin,          [], :int
  ffi_func :esp32_ota_check_version,  [:binstr, :size_t], :int
  ffi_func :esp32_ota_write,          [:binstr, :size_t], :int
  ffi_func :esp32_ota_end,            [:int], :int
  ffi_func :esp32_ota_running_slot,   [], :int
  ffi_func :esp32_ota_mark_valid,     [], :int
  ffi_func :esp32_ota_reboot,         [], :int
end

# ---- T-Display ST7789 (driver: firmware/main/tdisplay.c) ----
# Declared with the same FFI DSL. The C driver is only linked into the
# classic-ESP32 (T-Display) build, so every screen helper below is gated
# by esp32_board_has_lcd -- on the C3 the calls are never made.
module TDisplay
  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_width,  [], :int
  ffi_func :esp32_display_height, [], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_rect,  [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
end

# Panel driven in landscape 240x135 (set_orientation(1) transposes the
# MADCTL scan so x runs along the long glass edge -- the wide axis fits the
# full URL/host lines without wrapping). Colors: RGB565.
W = 240
H = 135

# Battery-present threshold. No pack wired to the divider reads ~0.0-0.3 V
# (floating pin); a real single-cell LiPo sits well above 3 V. Below the
# floor we print "NO BATTERY" instead of a bogus cell voltage.
BATT_MIN_V = 1.50

def batt_label(cell_v)
  return "NO BATTERY" if cell_v.nil? || cell_v < BATT_MIN_V
  "CELL #{cell_v}V"
end

# Boot/status screens. All helpers no-op silently when the board has no
# panel; esp32_display_* return IDF error codes which we log once.
$lcd_ok = false

def lcd_boot
  return false if ESP32.esp32_board_has_lcd == 0
  rc = TDisplay.esp32_display_init
  if rc != 0
    puts "LCD: init failed (#{rc}) -- continuing headless"
    return false
  end
  rc = TDisplay.esp32_display_set_orientation(1)   # landscape 240x135
  if rc != 0
    puts "LCD: orientation set failed (#{rc}) -- continuing as-is"
    return false
  end
  TDisplay.esp32_display_clear(0x0000)
  true
end

def lcd_show_booting(ssid)
  return unless $lcd_ok
  TDisplay.esp32_display_rect(0, 0, W, 28, 0x001F, 1)          # blue title bar
  TDisplay.esp32_display_text("TTGO T-DISPLAY", 6, 9, 0xFFFF, 0x001F, 1)
  TDisplay.esp32_display_text("SPINEL ESP32", 8, 44, 0x07E0, 0x0000, 2)
  TDisplay.esp32_display_text("WIFI: #{ssid[0, 32]}", 8, 84, 0xFFE0, 0x0000, 1)
  TDisplay.esp32_display_text("CONNECTING...", 8, 110, 0xFFFF, 0x0000, 1)
end

def lcd_show_running(ip, host, cell_v)
  return unless $lcd_ok
  TDisplay.esp32_display_clear(0x0000)
  TDisplay.esp32_display_rect(0, 0, W, 24, 0x001F, 1)          # blue title bar
  TDisplay.esp32_display_text("TTGO T-DISPLAY", 6, 7, 0xFFFF, 0x001F, 1)
  # the one thing the screen exists for: the address to open
  TDisplay.esp32_display_text("HTTP://", 6, 34, 0x07E0, 0x0000, 1)
  # IP auto-sizes: scale 2 (12 px/char + spacing) fits up to 19 chars;
  # a normal dotted quad (<=15) stays big.
  ip_scale = ip.size <= 18 ? 2 : 1
  TDisplay.esp32_display_text(ip, 54, 28, 0x07E0, 0x0000, ip_scale)
  TDisplay.esp32_display_text("OR", 6, 64, 0xFFFF, 0x0000, 1)
  # Hostname on one line: 240 px / 6 px per char = 40 chars at scale 1.
  TDisplay.esp32_display_text("HTTP://#{host}.local", 6, 80, 0x07E0, 0x0000, 1)
  TDisplay.esp32_display_text("SERVING / ON :80", 6, 100, 0xFFFF, 0x0000, 1)
  lcd_battery_line(cell_v)
end

def lcd_battery_line(cell_v)
  return unless $lcd_ok
  TDisplay.esp32_display_rect(0, H - 12, W, 12, 0x0000, 1)     # wipe footer
  TDisplay.esp32_display_text(batt_label(cell_v), 6, H - 12, 0xFFE0, 0x0000, 1)
end

def lcd_show_wifi_failed
  return unless $lcd_ok
  TDisplay.esp32_display_text("WIFI: NO LINK", 6, 96, 0xF800, 0x0000, 1)
  TDisplay.esp32_display_text("SERVING IP ONLY", 6, 112, 0xFFFF, 0x0000, 1)
end

# ---- buffers for the out-param FFI calls (String allocated in Ruby) ----
ssid_buf = "\0".b * 33
chan_buf = "\0".b * 8       # int slot (little-endian RV32; only chan written)
ip_buf   = "\0".b * 48
time_buf = "\0".b * 32      # "YYYY-MM-DD HH:MM:SS" + NUL from esp32_time_utc

def read_int_le(buf)
  # 4-byte little-endian int out of a :ptr buffer (RV32IMAC is LE).
  v = 0
  4.times { |i| v |= buf.getbyte(i) << (8 * i) }
  v
end

def status_page(ssid, rssi, chan, ip, led, cell_v, time_s, slot_s, ota)
  led_s  = led ? "on" : "off"
  rssi_s = rssi.nil? ? "?" : "#{rssi}"
  ot = ota ? "#{ota["phase"]} (#{ota["progress"]} bytes)" : "no attempt this boot"
  <<~HTML
    <!doctype html><html><head><title>spinel-esp32</title></head>
    <body style="font-family:monospace;background:#111;color:#eee;padding:2em">
    <h1>#{board_name} / spinel-esp32</h1>
    <table>
    <tr><td>LED (GPIO#{LED_PIN})</td><td>#{led_s}</td></tr>
    <tr><td>Battery cell</td><td>#{cell_v} V</td></tr>
    <tr><td>WiFi SSID</td><td>#{ssid}</td></tr>
    <tr><td>RSSI</td><td>#{rssi_s} dBm</td></tr>
    <tr><td>Channel</td><td>#{chan}</td></tr>
    <tr><td>IP</td><td>#{ip}</td></tr>
    <tr><td>Time (UTC)</td><td>#{time_s}</td></tr>
    <tr><td>Running slot</td><td>#{slot_s}</td></tr>
    <tr><td>Last OTA</td><td>#{ot}</td></tr>
    </table>
    <p><a href="/led/toggle" style="background:#eee;color:#111;padding:4px 12px;
       text-decoration:none">Toggle LED (currently #{led_s})</a></p>
    <p style="color:#999">OTA: /ota/start?url=host:port/spinel.bin &middot;
       /ota/status &middot; /ota/reboot</p>
    </body></html>
  HTML
end

# ---- bring-up ----
SSID = ESP32.esp32_wifi_ssid.delete("\0")
ESP32.esp32_gc_tune()   # M5c: GC triggers sized for the part, before churn starts
$lcd_ok = lcd_boot()    # T-Display: panel up before anything slow runs
if $lcd_ok
  lcd_show_booting(SSID)
  puts "LCD: booted, splash up"
end
ESP32.esp32_led_init(LED_PIN)
ESP32.esp32_led_write(LED_PIN, 1)
rc = ESP32.esp32_wifi_init()
if rc != 0
  puts "esp32_wifi_init failed: #{rc}"
  abort "wifi init failed"
end
ESP32.esp32_adc_init()
puts "Scanning (2.4G) ..."
n = ESP32.esp32_wifi_scan()
puts "Scan found #{n} networks"
ESP32.esp32_wifi_connect()
puts "Connecting to #{SSID} ..."
30.times do
  ESP32.esp32_delay_ms(500)
  break if ESP32.esp32_wifi_connected != 0
end
if ESP32.esp32_wifi_connected == 0
  puts "WiFi: connect timed out -- server will still listen"
  lcd_show_wifi_failed
else
  ESP32.esp32_wifi_ip(ip_buf, 48)
  ip = ip_buf.delete("\0").strip
  puts "WiFi: connected, IP #{ip}"
  # -- M5a: announce mdns_host + ".local" (mDNS) --
  host = mdns_host
  rc = ESP32.esp32_mdns_start(host, "spinel ESP32 status", ip)
  puts rc == 0 ? "mDNS: #{host}.local announced (_http._tcp :80)" \
               : "mDNS: start failed (#{rc}) -- serving on IP only"
  # -- M5b: start NTP, wall clock on the status page --
  ESP32.esp32_sntp_start()
  puts "NTP: querying pool.ntp.org ..."
end

server = TCPServer.new("0.0.0.0", PORT)
puts "Serving status on 0.0.0.0:#{PORT} (polling loop, no threads)"

# The screen's whole job: once the server is actually listening, show how
# to reach it -- numeric IP first, mDNS name as the second line. (The IP
# was already known above if WiFi joined; re-read it so the screen never
# shows an address the server cannot back.)
if ESP32.esp32_wifi_connected != 0
  ESP32.esp32_wifi_ip(ip_buf, 48)
  show_ip = ip_buf.delete("\0").strip
  mv = ESP32.esp32_adc_read_mv()
  cell0 = mv >= 0 ? ((mv * DIVIDER) / 1000.0).round(2) : 0.0
  lcd_show_running(show_ip, mdns_host, cell0)
  puts "LCD: running screen (#{show_ip} / #{mdns_host}.local)"
end

led = 1
loop_count = 0

# ---- M6: OTA download state ----
# ota_state is a tiny in-process record of the last /ota/start attempt:
#   nil = nothing attempted this boot;
#   { "slot" =>, "phase" =>, "progress" =>, "error" => } afterwards.
# "phase": downloading | sealed | failed. The image only boots on an
# explicit /ota/reboot (plan stage 3: do not auto-reboot).
ota_state = nil
ota_valid_latch = false   # Stage 4: proof-of-life mark-valid, once per boot

def ota_error(client, code, text)
  client.write "HTTP/1.1 #{code} #{text.split(' ').first}\r\n" \
               "Content-Type: text/plain\r\nConnection: close\r\n\r\n#{text}\n"
  client.close
end

# HTTP GET over spinel's socket layer, streaming straight into the other OTA
# slot. Memory discipline (plan): one reused String out-buffer for the
# body -- readpartial(n, buf) refills it in place -- plus small per-step
# temporaries only. No accumulated body in the Ruby heap, ever.
#
# The serving side for tests is `python3 -m http.server` on the LAN
# (plain HTTP; no TLS in scope). The request is HTTP/1.0-shaped, we
# parse Content-Length, verify the 0xE9 image magic in the first chunk
# BEFORE ota_begin, and hand each chunk to the C adapter via one
# esp32_ota_write(:binstr) call per chunk.
#
# Connect goes through the 1-arg Socket#connect_nonblock(sockaddr) path,
# NOT TCPSocket.new(host, port): sp_io_fdopen_sock hardcodes
# Errno::ECONNREFUSED for any negative fd, which masks the real lwIP
# error -- a DNS miss, no-route, or timeout all surfaced as "Connection
# refused". The 1-arg form calls connect(2) directly and raises the
# Errno::* class mapped from the true errno (hard errors raise even
# with exception: false); Socket.sockaddr_in raises SocketError on a
# failed name lookup before any socket exists.
def ota_connect(host, port, timeout_ms = 10000)
  sa = Socket.sockaddr_in(port, host)   # SocketError here on DNS failure
  sock = nil
  begin
    sock = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
    deadline = ESP32.esp32_millis + timeout_ms
    while true
      rc = sock.connect_nonblock(sa, exception: false)
      return sock if rc == 0   # handshake done (or lwIP answered EISCONN -> 0)
      # rc == :wait_writable: SYN in flight; keep probing. Hard errors
      # raise Errno::* right here (sp_io.c sp_sock_connect_nb_sa maps
      # errno -> Errno class even with exception: false).
      raise "connect deadline (#{timeout_ms} ms)" if ESP32.esp32_millis > deadline
      ESP32.esp32_delay_ms(50)
    end
  rescue => e
    # M6 leak fix: hard errors raised inside this loop (Errno::* from
    # connect, or the deadline) left `sock` (fd + lwIP netconn + PCB)
    # unreachable, and ota_download's ensure saw nil -- each failed
    # attempt permanently lost one socket (~4 KB of netconn buffers;
    # observed largest-block 7680 -> 3584 -> 2304 after two attempts).
    # Close ours before re-raising.
    puts "ota: ota_connect failed: #{e.class}: #{e.message} (closing fd)"
    begin
      sock.close if sock
    rescue StandardError
      nil
    end
    raise
  end
end

def ota_download(host, port, path, force: false)
  total = nil
  written = 0
  begun = false
  idle_ms = 15000   # no inbound byte for this long -> abort
  sock = nil
  begin
    sock = ota_connect(host, port)
    # A stalled server must not pin the download forever. lwIP's
    # SO_RCVTIMEO (0x1006) wants a `struct timeval`, not the bare int
    # spinel's setsockopt passes, so it fails with EINVAL -- so the
    # deadline lives in Ruby instead: every read is non-blocking and the
    # loop aborts when no byte has arrived for idle_ms. The old
    # slot keeps running on abort.
    sock.write "GET #{path} HTTP/1.0\r\nHost: #{host}:#{port}\r\n" \
               "User-Agent: spinel-esp32-ota/1\r\nAccept: */*\r\n\r\n"
    last_rx = ESP32.esp32_millis
    # ---- headers: non-blocking poll until \r\n\r\n, bounded ----
    hdr = ""
    while !hdr.include?("\r\n\r\n") && hdr.bytesize <= 8192
      chunk = sock.read_nonblock(512, exception: false)
      if chunk.nil?
        raise "no HTTP response (EOF in headers)" if hdr.empty?
        break
      elsif chunk == :wait_readable
        raise "idle timeout in headers" if ESP32.esp32_millis - last_rx > idle_ms
        ESP32.esp32_delay_ms(10)
        next
      end
      last_rx = ESP32.esp32_millis
      hdr += chunk
    end
    raise "no HTTP response (EOF in headers)" if hdr.empty?
    # The chunk that completes the headers can carry body bytes after
    # "\r\n\r\n"; dropping them desynchronises the image stream
    # (observed: "not an esp image (magic 20)" -- the magic check saw a
    # byte from deep inside the binary). Split them off and feed them
    # back as the first body chunk below.
    hsep = hdr.index("\r\n\r\n")
    raise "no HTTP response (unterminated headers)" unless hsep
    rest = hdr[(hsep + 4)..].to_s
    hdr = hdr[0, hsep + 4]
    status = hdr.split("\r\n").first.to_s
    raise "bad status line: #{status.inspect}" unless status.start_with?("HTTP/")
    code = status.split(" ")[1].to_i
    raise "server said #{code}" unless code == 200
    if hdr =~ /Content-Length:\s*(\d+)/i
      total = $1.to_i
      raise "bad Content-Length" if total <= 0
    end
    # ---- body: 4 KB non-blocking reads straight into ota_write ----
    first = true
    while true
      if first && !rest.empty?
        # body bytes that arrived with the final header chunk
        chunk = rest
        rest = ""
      else
        chunk = sock.read_nonblock(4096, exception: false)
      end
      if chunk.nil? || chunk == :wait_readable
        # would-block: the peer owes us bytes. A slow transfer still sends
        # *something* each tick, so the idle timer resets on every read.
        if ESP32.esp32_millis - last_rx > idle_ms
          raise "idle timeout after #{written} bytes"
        end
        ESP32.esp32_delay_ms(10)
        next
      end
      last_rx = ESP32.esp32_millis
      buf = chunk
      got = chunk.bytesize
      if first
        # esp image magic: 0xE9 at offset 0 -- abort BEFORE ota_begin on
        # anything else (a 404 page must never touch flash). The version
        # guard reads the app descriptor at offset 32, so accumulate the
        # first >= 80 bytes (24 image header + 8 segment header + 48
        # esp_app_desc_t) before checking; a server dribbling the image
        # out one tiny segment at a time must not trip the guard on a
        # short first read (native_ota_example's data_read > sizeof(...)
        # condition, made robust).
        while buf.bytesize < 80
          chunk = sock.read_nonblock(4096, exception: false)
          if chunk.nil? || chunk == :wait_readable
            raise "EOF before image header" if chunk.nil?
            raise "idle timeout before image header" if ESP32.esp32_millis - last_rx > idle_ms
            ESP32.esp32_delay_ms(10)
            next
          end
          last_rx = ESP32.esp32_millis
          buf += chunk
        end
        if buf.getbyte(0) != 0xE9
          raise "not an esp image (magic #{buf.getbyte(0).to_s(16)})"
        end
        # Version guard (native_ota_example): refuse to reflash the
        # running version, or one that previously failed to prove life
        # (last-invalid partition). Runs on the first bytes, before
        # ota_begin, so a refused image never touches flash.
        rc = ESP32.esp32_ota_check_version(buf, buf.bytesize)
        raise "version guard refused image (rc #{rc})" if rc != 0 && !force
        first = false
        got = buf.bytesize
      end
      if !begun
        rc = ESP32.esp32_ota_begin()
        raise "ota_begin failed (#{rc})" if rc != 0
        begun = true
      end
      rc = ESP32.esp32_ota_write(buf, got)
      raise "ota_write failed (#{rc})" if rc != 0
      written += got
      break if total && written >= total
    end
    raise "truncated: got #{written}, expected #{total}" if total && written < total
    # got == 0 (EOF) without a known total: accept, ota_end validates
    # the trailing image checksum anyway.
    rc = ESP32.esp32_ota_end(0)
    raise "ota_end failed (#{rc})" if rc != 0
    { "slot" => ESP32.esp32_ota_partition_name.delete("\0"), "phase" => "sealed",
      "progress" => written, "error" => nil }
  rescue => e
    if begun
      begin
        ESP32.esp32_ota_end(1)   # abort: leave the old slot running
      rescue StandardError
        nil
      end
    end
    { "slot" => begun ? ESP32.esp32_ota_partition_name.delete("\0") : nil,
      "phase" => "failed", "progress" => written, "error" => "#{e.class}: #{e.message}" }
  ensure
    begin
      sock.close if sock
    rescue StandardError
      nil
    end
  end
end

loop do
  # -- one non-blocking accept per tick --
  # M5c: accept itself can raise -- spinel's exception: false softens
  # only EAGAIN, and a peer that RSTs a connection still sitting in the
  # listen backlog makes lwip's accept fail with ENOTCONN (observed:
  # uncaught Errno::ENOTCONN "Socket is not connected @ accept" ->
  # abort() -> reboot, reproducibly). Rescue here so a dead backlogged
  # peer cannot reboot the board; the listener stays usable.
  client = nil
  begin
    client = server.accept_nonblock(exception: false)
  rescue => e
    puts "[#{loop_count}] accept error: #{e.class} #{e.message}"
  end
  if client && client != :wait_readable
    # M5c: a client that dies mid-exchange raises -- spinel's
    # read_nonblock(exception: false) softens only EAGAIN, and an
    # uncaught Errno aborts the VM (observed: ENOTCONN -> abort ->
    # reboot). Rescue so one dropped connection cannot reboot the board.
    begin
    req = ""
    # read until end of headers (best-effort, bounded)
    4.times do
      chunk = client.read_nonblock(512, exception: false)
      if chunk.is_a?(String)
        req += chunk
        break if req.include?("\r\n\r\n")
      end
    end
    rssi = ESP32.esp32_wifi_ap_info(ssid_buf, 33, chan_buf)
    run_slot = ESP32.esp32_ota_running_slot()
    slot_s = run_slot == 0 ? "ota_0" : run_slot == 1 ? "ota_1" : "factory"
    if rssi == -999
      rssi = nil
      chan = 0
      ssid = "(not associated)"
      ip = ""
    else
      ssid = ssid_buf.delete("\0").strip
      chan = read_int_le(chan_buf[0, 4])
      ESP32.esp32_wifi_ip(ip_buf, 48)
      ip = ip_buf.delete("\0").strip
    end

    # -- route: GET /led/toggle flips the LED; anything else serves index --
    path = req.split(" ")[1].to_s

    # -- M6: serve-path GC checkpoint #1 (after request read) --
    # The serve burst allocates ~4-5 KB (request parse + status strings +
    # page build) with no GC checkpoint inside it. On this part ~16 KB stays
    # free once WiFi/mDNS are up, and the pre-serve floor is ~9-10 KB, so an
    # unchecked burst bottoms near 1.2 KB and lwIP's TX allocations fail
    # (observed: "wifi:m f null" exactly at serve time, dropped responses,
    # ping loss). Checkpointing after the read clears the read/parse garbage
    # BEFORE the response allocates; the slab is off on ESP32 (shim mmap
    # answers MAP_FAILED -> sp_slab_on = 0), so freed spinel bytes hit the
    # IDF heap immediately -- no slab-chunk delay to wait out.
    ESP32.esp32_gc_step()

    # ---- M6 OTA routes (Stage 4 latch: first served request proves life) ----
    unless ota_valid_latch
      ota_valid_latch = true
      begin
        ESP32.esp32_ota_mark_valid()
      rescue => e
        puts "[#{loop_count}] ota mark_valid error: #{e.class} #{e.message}"
      end
    end

    if path.start_with?("/ota/start")
      # /ota/start?url=192.168.1.x:8000/spinel.bin[&force=1]  (LAN dev)
      # force=1 skips the version guard (same version as running is
      # normally refused; the round-trip reflash of an identical build
      # is a deliberate test case, so it gets an explicit bypass --
      # mirroring CONFIG_EXAMPLE_SKIP_VERSION_CHECK in the example).
      q = path.split("?", 2)[1].to_s
      murl = q =~ /url=([^&]+)/ ? $1 : nil
      url = murl.to_s
      force = q.include?("force=1")
      puts "[#{loop_count}] OTA start: #{url}"
      if url.empty?
        ota_error(client, 400, "missing url param")
      else
        hostport = url.split("/", 2)[0].to_s
        upath = "/" + (url.split("/", 2)[1].to_s)
        colon = hostport.rindex(":")
        ohost = colon ? hostport[0, colon] : hostport
        oport = colon ? hostport[colon + 1, hostport.size].to_i : 80
        oport = 80 if oport == 0
        # The download owns the client for its whole duration; report
        # when done. Progress prints to serial as it goes.
        puts "[#{loop_count}] OTA connecting #{ohost}:#{oport} #{upath}"
        ota_state = ota_download(ohost, oport, upath, force: force)
        puts "[#{loop_count}] OTA result: #{ota_state["phase"]} " \
             "#{ota_state["progress"]}B #{ota_state["error"]}"
        body = "OTA #{ota_state["phase"]}: #{ota_state["progress"]} bytes " \
               "#{ota_state["error"] ? "error=#{ota_state["error"]}" : "slot=#{ota_state["slot"]}"}\n"
        client.write "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" \
                     "Content-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n"
        client.write body
        client.close
      end
      puts "[#{loop_count}] served: #{req.split("\r\n")[0]}"
      next
    elsif path.start_with?("/ota/status")
      slot = ESP32.esp32_ota_running_slot()
      slot_s = slot == 0 ? "ota_0" : slot == 1 ? "ota_1" : "factory"
      st = ota_state || { "phase" => "idle", "progress" => 0, "error" => nil, "slot" => nil }
      body = "running slot: #{slot_s}\nlast ota: #{st["phase"]} " \
             "(#{st["progress"]} bytes#{st["error"] ? ", #{st["error"]}" : ""})\n" \
             "reboot pending\n"
      client.write "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" \
                   "Content-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n"
      client.write body
      client.close
      puts "[#{loop_count}] served: #{req.split("\r\n")[0]}"
      next
    elsif path.start_with?("/ota/reboot")
      body = "rebooting\n"
      client.write "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" \
                   "Content-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n"
      begin
        client.write body
        client.close
      rescue StandardError
        nil
      end
      puts "[#{loop_count}] OTA reboot requested"
      ESP32.esp32_delay_ms(100)
      ESP32.esp32_ota_reboot()
      next
    end

    mv = ESP32.esp32_adc_read_mv()
    cell_v = mv >= 0 ? ((mv * DIVIDER) / 1000.0).round(2) : 0.0
    # -- M5b: wall time (once NTP has synced) on the page --
    time_s = "(not synced)"
    if ESP32.esp32_sntp_synced() != 0
      rc = ESP32.esp32_time_utc(time_buf, 24)
      time_s = rc == 0 ? time_buf.delete("\0").strip : "(clock error)"
    end
    body = status_page(ssid, rssi, chan, ip, led != 0, cell_v, time_s,
                       slot_s, ota_state)
    client.write "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n" \
                 "Content-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n"
    client.write body
    client.close
    # -- M6: serve-path GC checkpoint #2 (after response) --
    # The page build + write leave a request's worth of strings and the
    # old body garbage on the string heap; reclaiming them here, rather
    # than at the next heartbeat (up to 1 s later), keeps the between-
    # serves floor at the heartbeat level instead of ratcheting down
    # ~200 B per serve (observed 9636 -> 9428 -> 9220 -> 4608).
    ESP32.esp32_gc_step()
    puts "[#{loop_count}] served: #{req.split("\r\n")[0]}"
    rescue => e
      # socket-level error (ENOTCONN/ECONNRESET/...): the exchange is dead,
      # but the server is not. Best-effort close, keep serving.
      puts "[#{loop_count}] client error: #{e.class} #{e.message}"
      begin
        client.close
      rescue StandardError
        nil
      end
    end
  end

  # -- slow-poll housekeeping: log battery; LED no longer blinks -- the
  #     toggle endpoint owns it now (state lives in this process) --
  if (loop_count % 50) == 0
    mv = ESP32.esp32_adc_read_mv()
    if mv >= 0
      cell = ((mv * DIVIDER) / 1000.0).round(2)
      puts "[#{loop_count}] idle; cell #{cell}V " \
           "heap #{ESP32.esp32_heap_free()} (min #{ESP32.esp32_heap_min_free()}, " \
           "largest #{ESP32.esp32_heap_largest_block()})"
      lcd_battery_line(cell)   # footer refresh ~ every 1 s (50 ticks x 20 ms)
    end
    # M6: one explicit collection per heartbeat -- bounds the decay
    # between churn-triggered runs to ~one heartbeat's garbage (~260 B)
    # instead of ~16 s' worth (~4 KB), so the IDF heap never dips near
    # the 300 B region where esp_wifi/lwIP allocations fail. Run AFTER
    # the heap log prints, so the next line shows the post-GC floor.
    ESP32.esp32_gc_step()
    ESP32.esp32_delay_ms(20)
  else
    ESP32.esp32_delay_ms(20)
  end
  loop_count += 1
end