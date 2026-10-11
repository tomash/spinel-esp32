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

# hotspot creds (Fairphone test): "Tomash Fairphone" / "railsgirls"
SSID     = ENV.fetch("WIFI_SSID",     "CzekamyNaAlvarium-asus4")
PASSWORD = ENV.fetch("WIFI_PASSWORD", "langsamabersicher")
PORT     = 80
LED_PIN  = 3
DIVIDER  = 2 # 100k/100k divider: cell = 2x pin voltage

module ESP32
  ffi_source <<~C
    #include "esp_wifi.h"
    #include "esp_event.h"
    #include "esp_netif.h"
    #include "nvs_flash.h"
    #include "esp_adc/adc_oneshot.h"
    #include "esp_adc/adc_cali.h"
    #include "string.h"

    /* ---- battery: GPIO2 = ADC1_CH2 on ESP32-C3 (NOT CH0; CH0 is GPIO0,
       a floating Grove pin -- the M4 under-read bug: CH0 drifted ~1.1-1.3V
       while the real divider output sat on CH2). Divider 2:1 (100k/100k),
       same as battery.rb ---- */
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
        if (adc_oneshot_config_channel(ws_adc_unit, ADC_CHANNEL_2, &chan) != ESP_OK)
            return -2;
        adc_cali_curve_fitting_config_t cc = {
            .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_2,
            .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12,
        };
        adc_cali_create_scheme_curve_fitting(&cc, &ws_adc_cali); /* optional */
        ws_adc_inited = 1;
        return 0;
    }

    /* Calibrated GPIO2 pin voltage in mV, or negative on failure. */
    int esp32_adc_read_mv(void) {
        if (!ws_adc_inited) return -1;
        int raw = -1;
        if (adc_oneshot_read(ws_adc_unit, ADC_CHANNEL_2, &raw) != ESP_OK) return -2;
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

    int esp32_wifi_init(const char *ssid, const char *pass) {
        if (nvs_flash_init() == ESP_ERR_NVS_NO_FREE_PAGES ||
            nvs_flash_init() == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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

  ffi_func :esp32_adc_init,     [], :int
  ffi_func :esp32_adc_read_mv, [], :int
  ffi_func :esp32_led_init,     [:int], :int
  ffi_func :esp32_led_write,    [:int, :int], :int
  ffi_func :esp32_wifi_init,     [:str, :str], :int
  ffi_func :esp32_wifi_connect, [], :int
  ffi_func :esp32_wifi_scan,    [], :int
  ffi_func :esp32_wifi_connected, [], :int
  ffi_func :esp32_wifi_ap_info,  [:ptr, :int, :ptr], :int
  ffi_func :esp32_wifi_ip,       [:ptr, :int], :int
  ffi_func :esp32_delay_ms,      [:int], :int, blocking: true
end

# ---- buffers for the out-param FFI calls (String allocated in Ruby) ----
ssid_buf = "\0".b * 33
chan_buf = "\0".b * 8       # int slot (little-endian RV32; only chan written)
ip_buf   = "\0".b * 48

def read_int_le(buf)
  # 4-byte little-endian int out of a :ptr buffer (RV32IMAC is LE).
  v = 0
  4.times { |i| v |= buf.getbyte(i) << (8 * i) }
  v
end

def status_page(ssid, rssi, chan, ip, led, cell_v)
  led_s  = led ? "on" : "off"
  rssi_s = rssi.nil? ? "?" : "#{rssi}"
  <<~HTML
    <!doctype html><html><head><title>spinel-esp32</title></head>
    <body style="font-family:monospace;background:#111;color:#eee;padding:2em">
    <h1>T-OI Plus / spinel-esp32</h1>
    <table>
    <tr><td>LED (GPIO#{LED_PIN})</td><td>#{led_s}</td></tr>
    <tr><td>Battery cell</td><td>#{cell_v} V</td></tr>
    <tr><td>WiFi SSID</td><td>#{ssid}</td></tr>
    <tr><td>RSSI</td><td>#{rssi_s} dBm</td></tr>
    <tr><td>Channel</td><td>#{chan}</td></tr>
    <tr><td>IP</td><td>#{ip}</td></tr>
    </table>
    <p><a href="/led/toggle" style="background:#eee;color:#111;padding:4px 12px;
       text-decoration:none">Toggle LED (currently #{led_s})</a></p>
    </body></html>
  HTML
end

# ---- bring-up ----
ESP32.esp32_led_init(LED_PIN)
ESP32.esp32_led_write(LED_PIN, 1)
rc = ESP32.esp32_wifi_init(SSID, PASSWORD)
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
else
  ESP32.esp32_wifi_ip(ip_buf, 48)
  ip = ip_buf.delete("\0").strip
  puts "WiFi: connected, IP #{ip}"
end

server = TCPServer.new("0.0.0.0", PORT)
puts "Serving status on 0.0.0.0:#{PORT} (polling loop, no threads)"

led = 1
loop_count = 0
loop do
  # -- one non-blocking accept per tick --
  client = server.accept_nonblock(exception: false)
  if client && client != :wait_readable
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
    if path.start_with?("/led/toggle")
      led = led == 1 ? 0 : 1
      ESP32.esp32_led_write(LED_PIN, led)
      puts "[#{loop_count}] LED toggled -> #{led}"
    end

    mv = ESP32.esp32_adc_read_mv()
    cell_v = mv >= 0 ? ((mv * DIVIDER) / 1000.0).round(2) : 0.0
    body = status_page(ssid, rssi, chan, ip, led != 0, cell_v)
    client.write "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n" \
                 "Content-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n"
    client.write body
    client.close
    puts "[#{loop_count}] served: #{req.split("\r\n")[0]}"
  end

  # -- slow-poll housekeeping: log battery; LED no longer blinks -- the
  #     toggle endpoint owns it now (state lives in this process) --
  if (loop_count % 50) == 0
    mv = ESP32.esp32_adc_read_mv()
    if mv >= 0
      puts "[#{loop_count}] idle; cell #{((mv * DIVIDER) / 1000.0).round(2)}V"
    end
    ESP32.esp32_delay_ms(20)
  else
    ESP32.esp32_delay_ms(20)
  end
  loop_count += 1
end