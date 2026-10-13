/* shim/esp32/include/sys/socket.h -- newlib (riscv32-esp-elf) ships no
 * <sys/socket.h>; the ESP-IDF build proper gets it from lwip. This shim is
 * what the standalone RT cross-build compiles against -- declarations and
 * types only, no definitions, so the final firmware link resolves the calls
 * to lwip's (lwip_socket, aliased to socket).
 *
 * The structs and constants byte-match lwip exactly (lwip/sockets.h):
 * sockaddr_in carries sin_len (LWIP_SOCKET_HAVE_SA_LEN=1), socklen_t is
 * u32, sa_family_t is u8. RT objects pass these structs to lwip code by
 * reference, so layout agreement is not optional.
 *
 * Omitted on purpose: IPv6 structs (AF_INET6 == AF_UNSPEC with LWIP_IPV6
 * off in the default single-at-a-time ESP-IDF config; code that uses them
 * is #ifdef-guarded by the AF_INET6 value mismatch), socketpair() (no
 * lwip counterpart; grep shows no spinel caller), SO_NWRITE (Darwin).
 *
 * MSG_DONTROUTE is omitted: lwip does not define it, and sp_io.c guards
 * the only use with #ifdef.
 */
#ifndef _SP_SHIM_SYS_SOCKET_H
#define _SP_SHIM_SYS_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>      /* fd_set for select-based decls; also struct timeval */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types (lwip shapes) ---------------------------------------------- */
typedef unsigned char  sa_family_t;
typedef unsigned short in_port_t;
typedef unsigned int   socklen_t;

struct in_addr { unsigned int s_addr; };

struct sockaddr {
  unsigned char sa_len;
  sa_family_t   sa_family;
  char          sa_data[14];
};

struct sockaddr_in {
  unsigned char   sin_len;
  sa_family_t    sin_family;
  in_port_t      sin_port;
  struct in_addr sin_addr;
  char            sin_zero[8];
};

/* IPv6: sp_net.c casts to sockaddr_in6 unconditionally (ss_family compare),
 * so the struct must exist even when the target netstack has IPv6 off.
 * Byte layout matches lwip's (sin6_len + family + port + flowinfo + 16-byte
 * address + scope id = 24 bytes). */
struct in6_addr { unsigned char s6_addr[16]; };

struct sockaddr_in6 {
  unsigned char    sin6_len;
  sa_family_t     sin6_family;
  in_port_t       sin6_port;
  unsigned int    sin6_flowinfo;
  struct in6_addr sin6_addr;
  unsigned int    sin6_scope_id;
};

struct sockaddr_storage {
  unsigned char s2_len;
  sa_family_t   ss_family;
  char          s2_data1[2];
  unsigned int  s2_data2[3];
  unsigned int  s2_data3[3];   /* IPv6 tail; keeps sizeof == sockaddr_in6 */
};

/* ssize_t: newlib defines it in <sys/types.h> -- already included above. */

struct linger {
  int l_onoff;
  int l_linger;
};

/* ---- address/protocol families (lwip values) --------------------------- */
#define AF_UNSPEC     0
#define AF_INET       2
#define AF_INET6      10      /* LWIP_IPV6=0 in the ESP-IDF default config:
                                  still defined, guarded code compares first */
#define AF_UNIX       1       /* glibc has it in sys/socket.h; sp_cold.c's
                                  Addrinfo.unix reads it without sys/un.h.
                                  Same value as sys/un.h: identical
                                  redefinition is harmless */
#define PF_UNSPEC     AF_UNSPEC
#define PF_INET       AF_INET
#define PF_INET6      AF_INET6
#define PF_UNIX       AF_UNIX   /* sp_io.c's sp_sock_const names it; lwip has no
                                   UNIX-domain sockets, so the value never
                                   reaches a real socket() call: Socket.new
                                   with AF_UNIX fails at runtime with the
                                   socket family, honestly */

/* ---- socket types (lwip) ----------------------------------------------- */
#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define SOCK_RAW      3

/* ---- listen backlog ----------------------------------------------------- */
/* ESP-IDF defines SOMAXCONN in <net/if.h> (components/newlib/platform_include/
 * net/if.h:20, value 128); the standalone RT cross-build has no <net/if.h> in
 * scope, and upstream sp_net_listen_all passes it to listen(2). Match IDF's
 * value exactly so a future firmware build resolves the same backlog. */
#ifndef SOMAXCONN
#define SOMAXCONN     128
#endif

/* ---- shutdown (lwip) ---------------------------------------------------- */
#define SHUT_RD       0
#define SHUT_WR       1
#define SHUT_RDWR     2

/* ---- levels (lwip) ------------------------------------------------------ */
#define SOL_SOCKET    0xfff
#define IPPROTO_IP    0
#define IPPROTO_ICMP  1
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17
#define IPPROTO_IPV6  41

/* ---- SO_ options (lwip values) ------------------------------------------ */
#define SO_DEBUG       0x0001
#define SO_ACCEPTCONN  0x0002
#define SO_REUSEADDR   0x0004
#define SO_KEEPALIVE   0x0008
#define SO_DONTROUTE   0x0010
#define SO_USELOOPBACK 0x0040
#define SO_LINGER      0x0080
#define SO_OOBINLINE   0x0100
#define SO_REUSEPORT   0x0200
#define SO_BROADCAST   0x0020   /* lwip: SO_BROADCAST 0x0020 (lwip_sockopts.h) */
#define SO_SNDBUF      0x1001
#define SO_RCVBUF      0x1002
#define SO_SNDLOWAT    0x1003
#define SO_RCVLOWAT    0x1004
#define SO_SNDTIMEO    0x1005
#define SO_RCVTIMEO    0x1006
#define SO_ERROR       0x1007
#define SO_TYPE        0x1008

/* ---- send/recv flags (lwip values) --------------------------------------- */
#define MSG_PEEK      0x01
#define MSG_WAITALL   0x02
#define MSG_OOB       0x04
#define MSG_DONTWAIT  0x08
#define MSG_MORE      0x10
#define MSG_NOSIGNAL  0x20

/* ---- declarations (resolved by lwip at the firmware link) ---------------- */
int      socket(int domain, int type, int protocol);
int      bind(int s, const struct sockaddr *name, socklen_t namelen);
int      listen(int s, int backlog);
int      accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int      connect(int s, const struct sockaddr *name, socklen_t namelen);
int      shutdown(int s, int how);
int      setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int      getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int      getpeername(int s, struct sockaddr *name, socklen_t *namelen);
int      getsockname(int s, struct sockaddr *name, socklen_t *namelen);
ssize_t  send(int s, const void *dataptr, size_t size, int flags);
ssize_t  sendto(int s, const void *dataptr, size_t size, int flags,
               const struct sockaddr *to, socklen_t tolen);
ssize_t  recv(int s, void *mem, size_t len, int flags);
ssize_t  recvfrom(int s, void *mem, size_t len, int flags,
                  struct sockaddr *from, socklen_t *fromlen);

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_SYS_SOCKET_H */
