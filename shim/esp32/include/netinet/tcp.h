/* shim/esp32/include/netinet/tcp.h -- newlib ships none; lwip's values
 * (lwip/sockets.h "Options for level IPPROTO_TCP"). LWIP_TCP_KEEPALIVE=0
 * in the ESP-IDF default config, so TCP_KEEPIDLE/INTVL/KEEPCNT/KEEPALIVE
 * compile out via the #ifdef guards in sp_io.c's constant table -- only
 * TCP_NODELAY is unguarded.
 */
#ifndef _SP_SHIM_NETINET_TCP_H
#define _SP_SHIM_NETINET_TCP_H

#define TCP_NODELAY 0x01    /* don't delay send to coalesce packets */
#define TCP_KEEPALIVE 0x02 /* send KEEPALIVE probes when idle (ms) */
#define TCP_KEEPIDLE  0x03 /* idle before first probe, seconds */
#define TCP_KEEPINTVL 0x04 /* between probes, seconds */
#define TCP_KEEPCNT    0x05 /* probes before giving up */

#endif /* _SP_SHIM_NETINET_TCP_H */
