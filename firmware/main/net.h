/* firmware/main/net.h -- the networking and system API the Ruby apps call.
 *
 * Like board.h, one header and one implementation per SDK:
 *   ESP-IDF   firmware/main/net_esp.c   (esp_wifi, espressif/mdns, esp_sntp)
 *   Pico SDK  firmware-pico/main/net_pico.c (CYW43 + lwIP mdns/sntp)
 * so app/wifi_status.rb is the same Ruby on both. Apps declare what they use
 * with ffi_func, e.g.  ffi_func :net_wifi_connected, [], :int
 *
 * Credentials come from wifi_secrets.h (WIFI_SSID / WIFI_PASSWORD; written by
 * tools/build-app.sh from the environment or .env).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* WiFi station. net_wifi_init brings the radio up (no connect yet);
 * net_wifi_connect starts an asynchronous connect, retried on failure;
 * net_wifi_connected is 1 once an address is assigned. */
const char *net_wifi_ssid(void);
int net_wifi_init(void);
int net_wifi_connect(void);
int net_wifi_connected(void);
/* Blocking scan, printing one "SCAN: ..." line per AP; answers the count. */
int net_wifi_scan(void);
/* Associated AP: ssid into ssid_out (cap bytes), channel into *chan_out
 * (0 when the SDK cannot tell); answers RSSI in dBm, -999 if not associated. */
int net_wifi_ap_info(char *ssid_out, int cap, int *chan_out);
/* Station IPv4 address as text; 0 on success. */
int net_wifi_ip(char *ipbuf, int cap);

/* mDNS: claim <hostname>.local and advertise _http._tcp on port 80 with an
 * "ip" TXT record. SNTP from pool.ntp.org; net_sntp_synced latches 1 after
 * the first successful sync. net_time_utc: "YYYY-MM-DD HH:MM:SS" UTC. */
int net_mdns_start(const char *hostname, const char *instance, const char *ip);
int net_sntp_start(void);
int net_sntp_synced(void);
int net_time_utc(char *buf, int cap);

/* Task delay and a free-running millisecond clock (wraps after ~49 days). */
int sys_delay_ms(int ms);
int sys_millis(void);

/* Heap budget shared by the runtime and the network stack: free bytes now,
 * the lowest free seen, and the largest single block malloc can return. */
int sys_heap_free(void);
int sys_heap_min_free(void);
int sys_heap_largest_block(void);

/* GC pacing for a long-running networked app: sys_gc_tune aims the
 * runtime's churn triggers at the part's RAM, sys_gc_step runs a full
 * collection plus string sweep (call it once per heartbeat). */
int sys_gc_tune(void);
int sys_gc_step(void);

/* Over-the-air update of the running image (ESP-IDF's two-slot OTA). On a
 * platform without it every call answers -1 and the name is "none". */
const char *ota_partition_name(void);
int ota_begin(void);
int ota_check_version(const uint8_t *buf, size_t len);
int ota_write(const uint8_t *buf, size_t len);
int ota_end(int abort);
int ota_running_slot(void);
int ota_mark_valid(void);
int ota_reboot(void);
