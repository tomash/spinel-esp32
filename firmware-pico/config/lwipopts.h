/* firmware-pico/config/lwipopts.h -- lwIP for spinel-mote on the Pico 2 W:
 * the BSD socket API on FreeRTOS (pico_cyw43_arch_lwip_sys_freertos), as the
 * ESP-IDF build has it. Based on pico-examples' lwipopts_examples_common.h.
 *
 * The runtime archive was compiled against shim/esp32's socket headers,
 * whose struct layouts and SOL_/SO_/MSG_ values are lwIP's; the poll bits
 * and O_NONBLOCK are newlib's and are translated in sp_pico_sockets.c. */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      0
#define LWIP_SOCKET                 1
#define LWIP_NETCONN                1   /* the socket API is built on it */
/* POSIX names come from sp_pico_sockets.c as real functions (the runtime
 * archive references them out of line), not lwIP's macros. */
#define LWIP_COMPAT_SOCKETS         0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
/* fds 0-2 are stdio: sockets start above them so one fd space serves both */
#define LWIP_SOCKET_OFFSET          16
#define LWIP_SOCKET_POLL            1
#define LWIP_SOCKET_SELECT          1
#define MEMP_NUM_NETCONN            12
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO            1
#define LWIP_SO_RCVBUF              1
#define SO_REUSE                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_TIMEVAL_PRIVATE        0
/* errno values from newlib's <errno.h> (lwIP's own table is Linux's, e.g.
 * EINPROGRESS 115, where the runtime was compiled with newlib's 119) */
#define LWIP_ERRNO_STDINCLUDE       1
/* pico_cyw43_arch defines LWIP_PROVIDE_ERRNO=1 on the command line (for
 * OS-less builds), which would swap in that Linux table: lwip/errno.h tests
 * it after including this file, so undo it here. */
#undef LWIP_PROVIDE_ERRNO

#define TCPIP_THREAD_STACKSIZE      2048
#define TCPIP_THREAD_PRIO           8
#define DEFAULT_THREAD_STACKSIZE    1024
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8
#define TCPIP_MBOX_SIZE             16
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16000
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

/* mDNS responder (LWIP_MDNS_RESPONDER) and SNTP for wifi_status */
#define LWIP_IGMP                   1
#define LWIP_MDNS_RESPONDER         1
#define LWIP_NUM_NETIF_CLIENT_DATA  1
#define MDNS_MAX_SERVICES           2
#define SNTP_SERVER_DNS             1
void sp_pico_sntp_set_time(unsigned long sec, unsigned long us);
#define SNTP_SET_SYSTEM_TIME_US(sec, us) sp_pico_sntp_set_time((sec), (us))

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#endif /* _LWIPOPTS_H */
