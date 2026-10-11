/* shim/esp32/include/netinet/in.h -- newlib ships none; matches lwip's
 * netinet/in.h (which includes lwip/inet.h for the sockaddr_in family)
 * struct-for-struct, values-for-values. Declaration-only: INADDR_* and
 * in_port_t come via <sys/socket.h>, which every caller includes first
 * (sp_net.c, sp_io.c both do).
 */
#ifndef _SP_SHIM_NETINET_IN_H
#define _SP_SHIM_NETINET_IN_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INADDR_ANY       ((unsigned long)0x00000000)   /* 0.0.0.0, lwip: IPADDR_ANY */
#define INADDR_NONE      ((unsigned long)0xffffffff)   /* 255.255.255.255 */
#define INADDR_LOOPBACK  ((unsigned long)0x7f000001)   /* 127.0.0.1 */
#define INADDR_BROADCAST ((unsigned long)0xffffffff)

#define htons(x) ((unsigned short)((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#define htonl(x) ((((x) & 0xffUL) << 24) | \
                  (((x) & 0xff00UL) << 8) | \
                  (((x) >> 8) & 0xff00UL) | \
                  (((x) >> 24) & 0xffUL))
#define ntohs(x) htons(x)
#define ntohl(x) htonl(x)

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_NETINET_IN_H */
