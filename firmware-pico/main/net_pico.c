/*
 * net_pico.c -- firmware/main/net.h on the Pico 2 W: CYW43439 station mode
 * through pico_cyw43_arch (lwIP sockets on FreeRTOS), lwIP's mDNS responder
 * and SNTP client, heap figures from newlib's mallinfo.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "net.h"
#include "sp_pico.h"

/* shim/rp2350/src/sp_rp2350_stub.c (newlib declares them only under
 * _POSIX_TIMERS, which the SDK's sources are not built with) */
int clock_gettime(clockid_t clk, struct timespec *ts);
int clock_settime(clockid_t clk, const struct timespec *ts);
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif

#if __has_include("wifi_secrets.h")
  #include "wifi_secrets.h"
#else
  /* apps without WiFi build without credentials; net_wifi_init refuses */
  #define WIFI_SSID     ""
  #define WIFI_PASSWORD ""
#endif

/* ---- cyw43 bring-up (shared with boards/pico2w.c) ------------------------ */
static int s_cyw43 = 0;   /* 0 not tried, 1 up, <0 failed */
int sp_pico_cyw43_up(void)
{
    if (s_cyw43 == 0) s_cyw43 = cyw43_arch_init() == 0 ? 1 : -1;
    return s_cyw43 == 1 ? 0 : -1;
}

static struct netif *sta_netif(void) { return &cyw43_state.netif[CYW43_ITF_STA]; }

/* ---- WiFi station -------------------------------------------------------- */
static int s_retries = 0;

const char *net_wifi_ssid(void) { return WIFI_SSID; }

int net_wifi_init(void)
{
    if (WIFI_SSID[0] == 0) {
        printf("net: no WIFI_SSID (firmware/main/wifi_secrets.h)\n");
        return -7;
    }
    if (sp_pico_cyw43_up() != 0) return -1;
    cyw43_arch_enable_sta_mode();
    /* No power save, as the ESP build (WIFI_PS_NONE): a dozing station
     * answers pings and HTTP late. */
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    return 0;
}

int net_wifi_connect(void)
{
    return cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD,
                                         CYW43_AUTH_WPA2_AES_PSK) == 0 ? 0 : -1;
}

int net_wifi_connected(void)
{
    int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (st == CYW43_LINK_UP) return 1;
    /* failed joins (no AP, bad auth): retry a bounded number of times, as
     * the ESP event handler does. Not on CYW43_LINK_DOWN: that is also what
     * a join still in progress reads, and re-issuing it every poll restarts
     * the join under the driver's feet (it hung the radio). */
    if ((st == CYW43_LINK_FAIL || st == CYW43_LINK_NONET || st == CYW43_LINK_BADAUTH) &&
        s_retries < 40) {
        s_retries++;
        net_wifi_connect();
    }
    return 0;
}

/* The CYW43 reports every beacon it hears, so one AP shows up many times:
 * print each SSID once. The channel of our own SSID is remembered for
 * net_wifi_ap_info (the driver has no channel query). */
#define SCAN_MAX 32
static struct { int count; } s_scan;
static uint8_t s_seen[SCAN_MAX][33];
static int s_ap_channel = 0;

static int scan_cb(void *env, const cyw43_ev_scan_result_t *r)
{
    (void)env;
    char ssid[33];
    size_t n = r->ssid_len < 32 ? r->ssid_len : 32;
    memcpy(ssid, r->ssid, n);
    ssid[n] = 0;
    if (strcmp(ssid, WIFI_SSID) == 0) s_ap_channel = r->channel;
    for (int i = 0; i < s_scan.count; i++)
        if (strcmp((const char *)s_seen[i], ssid) == 0) return 0;
    if (s_scan.count < SCAN_MAX) memcpy(s_seen[s_scan.count++], ssid, n + 1);
    const char *am = r->auth_mode == 0 ? "OPEN" : (r->auth_mode & 4) ? "WPA2" :
                     (r->auth_mode & 2) ? "WPA" : "WEP";
    printf("SCAN: %-24.24s rssi %4d ch %2d %s\n", ssid[0] ? ssid : "(hidden)",
           (int)r->rssi, (int)r->channel, am);
    return 0;
}

int net_wifi_scan(void)
{
    if (sp_pico_cyw43_up() != 0) return -1;
    s_scan.count = 0;
    cyw43_wifi_scan_options_t opts = {0};
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb) != 0) return -2;
    while (cyw43_wifi_scan_active(&cyw43_state)) vTaskDelay(pdMS_TO_TICKS(50));
    return s_scan.count;
}

int net_wifi_ap_info(char *ssid_out, int cap, int *chan_out)
{
    if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_JOIN) return -999;
    int32_t rssi = 0;
    if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) != 0) return -999;
    memset(ssid_out, 0, (size_t)cap);
    strncpy(ssid_out, WIFI_SSID, (size_t)(cap - 1));
    /* chan_out points into a Ruby String's bytes, which need not be 4-byte
     * aligned, and the Hazard3 cores trap on a misaligned store (the C3
     * does not): copy, never store through the int pointer. */
    int chan = s_ap_channel;   /* from the last scan; 0 if none saw the AP */
    if (chan_out) memcpy(chan_out, &chan, sizeof chan);
    return (int)rssi;
}

int net_wifi_ip(char *ipbuf, int cap)
{
    const ip4_addr_t *a = netif_ip4_addr(sta_netif());
    if (!a || ip4_addr_isany(a)) return -1;
    return ip4addr_ntoa_r(a, ipbuf, cap) ? 0 : -2;
}

/* ---- mDNS ---------------------------------------------------------------- */
static char s_mdns_ip[20];

static void mdns_txt(struct mdns_service *svc, void *arg)
{
    (void)arg;
    char rec[28];
    int n = snprintf(rec, sizeof rec, "ip=%s", s_mdns_ip);
    mdns_resp_add_service_txtitem(svc, rec, (u8_t)n);
}

int net_mdns_start(const char *hostname, const char *instance, const char *ip)
{
    strncpy(s_mdns_ip, ip, sizeof s_mdns_ip - 1);
    int rc = 0;
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    if (mdns_resp_add_netif(sta_netif(), hostname) != ERR_OK) rc = -2;
    else if (mdns_resp_add_service(sta_netif(), instance, "_http", DNSSD_PROTO_TCP,
                                   80, mdns_txt, NULL) < 0) rc = -4;
    cyw43_arch_lwip_end();
    return rc;
}

/* ---- SNTP and wall time ---------------------------------------------------- */
static volatile int s_time_synced = 0;

/* lwipopts.h SNTP_SET_SYSTEM_TIME_US: runs in the tcpip thread. */
void sp_pico_sntp_set_time(unsigned long sec, unsigned long us)
{
    struct timespec ts = { .tv_sec = (time_t)sec, .tv_nsec = (long)us * 1000 };
    clock_settime(CLOCK_REALTIME, &ts);
    s_time_synced = 1;
}

int net_sntp_start(void)
{
    cyw43_arch_lwip_begin();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    cyw43_arch_lwip_end();
    return 0;
}

int net_sntp_synced(void) { return s_time_synced; }

/* newlib's time()/gettimeofday() read the same clock as the runtime's
 * clock_gettime(CLOCK_REALTIME) (shim/rp2350/src/sp_rp2350_stub.c), so
 * Time.now and time(NULL) agree once SNTP has set it. */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

int net_time_utc(char *buf, int cap)
{
    time_t t = time(NULL);
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) return -1;
    return strftime(buf, (size_t)cap, "%Y-%m-%d %H:%M:%S", &tm) == 0 ? -2 : 0;
}

/* ---- system ------------------------------------------------------------------ */
int mote_delay_ms(int ms)
{
    TickType_t t = pdMS_TO_TICKS((uint32_t)ms);
    vTaskDelay(t ? t : 1);
    return 0;
}

int mote_millis(void) { return (int)to_ms_since_boot(get_absolute_time()); }

/* The heap is newlib's, from the end of .bss (__end__) to __HeapLimit; the
 * part above the current break has never been handed out, so it is one
 * free block. */
extern char __end__, __HeapLimit;
static int s_heap_min = -1;

static int heap_free_now(void)
{
    struct mallinfo mi = mallinfo();
    char *brk = sbrk(0);
    return (int)(&__HeapLimit - brk) + (int)mi.fordblks;
}

int mote_heap_free(void)
{
    int f = heap_free_now();
    if (s_heap_min < 0 || f < s_heap_min) s_heap_min = f;
    return f;
}

int mote_heap_min_free(void) { mote_heap_free(); return s_heap_min; }

int mote_heap_largest_block(void)
{
    /* a lower bound: the untouched region above the break */
    return (int)(&__HeapLimit - (char *)sbrk(0));
}

/* GC pacing: the RP2350 has ~400 KB of heap once WiFi is up (the C3 has
 * ~16 KB), so the triggers sit 8x higher than net_esp.c's. */
extern size_t sp_gc_threshold, sp_gc_threshold_init;
extern size_t sp_str_threshold, sp_str_threshold_init;
extern size_t sp_str_old_threshold, sp_str_old_threshold_init;
int mote_gc_tune(void)
{
    sp_gc_threshold      = 64 * 1024; sp_gc_threshold_init      = 64 * 1024;
    sp_str_threshold     = 32 * 1024; sp_str_threshold_init     = 32 * 1024;
    sp_str_old_threshold = 64 * 1024; sp_str_old_threshold_init = 64 * 1024;
    return 0;
}

void sp_gc_collect_request(void);
void sp_str_sweep(void);
int mote_gc_step(void)
{
    sp_gc_collect_request();
    sp_str_sweep();
    return 0;
}

/* ---- OTA: not on the Pico yet (no partition table in this firmware) ----- */
const char *ota_partition_name(void) { return "none"; }
int ota_begin(void) { return -1; }
int ota_check_version(const uint8_t *buf, size_t len) { (void)buf; (void)len; return -1; }
int ota_write(const uint8_t *buf, size_t len) { (void)buf; (void)len; return -1; }
int ota_end(int abort) { (void)abort; return -1; }
int ota_running_slot(void) { return -1; }
int ota_mark_valid(void) { return -1; }
int ota_reboot(void) { return -1; }
