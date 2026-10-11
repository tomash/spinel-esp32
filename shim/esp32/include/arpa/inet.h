/* shim/esp32/include/arpa/inet.h -- newlib ships none. Declaration-only,
 * lwip-compatible: inet_ntop/inet_pton resolve to lwip's (lwip_inet_ntop /
 * lwip_inet_pton) at the firmware link; inet_ntoa/aton/addr are newlib's.
 */
#ifndef _SP_SHIM_ARPA_INET_H
#define _SP_SHIM_ARPA_INET_H

#include <sys/socket.h>    /* in_addr, socklen_t, AF_* */
#include <netinet/in.h>    /* htons/htonl */

#ifdef __cplusplus
extern "C" {
#endif

/* conversion functions (newlib's libc provides these) */
unsigned long inet_addr(const char *cp);
int           inet_aton(const char *cp, struct in_addr *addr);
char         *inet_ntoa(struct in_addr in);

/* lwip provides these in firmware */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int         inet_pton(int af, const char *src, void *dst);

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_ARPA_INET_H */
