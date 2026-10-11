/* shim/esp32/include/netdb.h -- newlib ships none. lwip's netdb.h is the
 * model: struct addrinfo/hostent with lwip's exact shapes, EAI_* from its
 * error block, AI_* flags. getaddrinfo/freeaddrinfo resolve to lwip's
 * (lwip_getaddrinfo/lwip_freeaddrinfo) at the firmware link.
 *
 * NI_MAXHOST must exist (sp_net.c:45 declares char buf[NI_MAXHOST]).
 *
 * Deliberate omission: NI_NAMEREQD and getnameinfo. lwip has no
 * getnameinfo, and sp_net.c guards its reverse-lookup block with
 * #ifdef NI_NAMEREQD -- leaving it undefined compiles that path out to
 * the documented numeric-address fallback. No symbol can dangle.
 */
#ifndef _SP_SHIM_NETDB_H
#define _SP_SHIM_NETDB_H

#include <sys/socket.h>    /* socklen_t, sockaddr */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- errors (lwip's values; h_errno-compatible) ------------------------- */
#define EAI_NONAME      200
#define EAI_SERVICE     201
#define EAI_FAIL        202
#define EAI_MEMORY      203
#define EAI_FAMILY      204

#define HOST_NOT_FOUND 210
#define NO_DATA        211
#define NO_RECOVERY    212
#define TRY_AGAIN      213

/* ---- flags (lwip) ------------------------------------------------------- */
#define AI_PASSIVE     0x01
#define AI_CANONNAME   0x02
#define AI_NUMERICHOST 0x04
#define AI_NUMERICSERV 0x08
#define AI_V4MAPPED    0x10
#define AI_ALL         0x20
#define AI_ADDRCONFIG  0x40

/* ---- name length -------------------------------------------------------- */
#define NI_MAXHOST  64     /* lwip: DNS_MAX_NAME_LENGTH (default 64 via Kconfig) */
#define NI_MAXSERV  32

/* ---- structs (lwip shapes) ---------------------------------------------- */
struct hostent {
  char  *h_name;
  char **h_aliases;
  int    h_addrtype;
  int    h_length;
  char **h_addr_list;
#define h_addr h_addr_list[0]
};

struct addrinfo {
  int               ai_flags;
  int               ai_family;
  int               ai_socktype;
  int               ai_protocol;
  socklen_t         ai_addrlen;
  struct sockaddr  *ai_addr;
  char             *ai_canonname;
  struct addrinfo  *ai_next;
};

/* ---- declarations (lwip in firmware) ----------------------------------- */
struct hostent *gethostbyname(const char *name);
void freeaddrinfo(struct addrinfo *ai);
int  getaddrinfo(const char *nodename, const char *servname,
                 const struct addrinfo *hints, struct addrinfo **res);

extern int h_errno;

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_NETDB_H */
