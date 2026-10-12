/*
 * sp_lwip_compat.c -- POSIX socket API adapter for the prebuilt spinel RT
 * archive (M4).
 *
 * WHY THIS FILE EXISTS
 * The RT archive (libspinel_rt_esp32.a) was cross-compiled against the
 * shim headers (shim/esp32/include), which declare the BSD socket API
 * under its POSIX names: socket, bind, listen, accept, connect, ...
 * ESP-IDF's compiled liblwip.a exports ONLY the lwip_* spellings (nm:
 * 67 T-symbols, zero plain POSIX socket names; IDF's own components see
 * POSIX names as static inline wrappers in components/lwip/include/
 * lwip/sockets.h, which live in each compiling TU and vanish). The RT
 * objects, already compiled, reference the plain out-of-line names --
 * the final link fails with "undefined reference to `socket'" et al.
 *
 * This TU defines the missing spellings as out-of-line wrappers onto the
 * lwip_* entry points. They are exactly what IDF's inline wrappers do,
 * but with external linkage so archive objects can bind against them.
 *
 * WHY NO lwip HEADERS ARE INCLUDED
 * main's INCLUDE_DIRS puts shim/esp32/include first, and lwip's wrapper
 * header would collide: it declares the same POSIX names as static
 * inline functions -- a non-static redefinition is a hard C error
 * (redefinition of 'socket', 6.2.2.1). Instead this TU declares the
 * minimal ABI surface itself: socklen_t u32, struct sockaddr with the
 * lwip layout (LWIP_SOCKET_HAVE_SA_LEN=1: sa_len byte + sa_family byte
 * + 14 data bytes), sockaddr_in byte-identical to the shim's, struct
 * addrinfo with lwip's field order, struct stat forward-declared (its
 * full shape belongs to newlib's <sys/stat.h>, which we do include --
 * it carries no socket decls, so no conflict), and the lwip_* entry
 * points as extern prototypes with the signatures from lwip/sockets.h
 * (LWIP_COMPAT_SOCKETS=0 in ESP-IDF's lwipopts.h -- no macro renames).
 * On RV32 ILP32: int 32, long 32, ssize_t long, size_t unsigned --
 * identical to how the RT archive was compiled.
 *
 * VFS fd NOTE
 * CONFIG_VFS_SUPPORT_IO=1: vfs_lwip.c registers lwip sockets into the
 * VFS fd table (range [LWIP_SOCKET_OFFSET, MAX_FDS)), and plain read/
 * write/close/fcntl/ioctl/select on those fds route through the VFS
 * layer. lwip_* socket calls use the SAME fd numbering (sockets.c adds
 * LWIP_SOCKET_OFFSET internally), so no fd translation is needed here.
 *
 * poll() is NOT wrapped: libnewlib.a (newlib/src/poll.c) implements it
 * over select() and hence the VFS, which handles socket fds and leaves
 * console fds alone. The RT archive sits LAST on the IDF link line, so
 * without intervention the one-shot archive scan misses newlib's poll
 * object; -Wl,-u,poll in CMakeLists anchors it in. lwip_poll exists as
 * the sockets-only fallback if that anchor is ever dropped.
 *
 * lstat(): newlib declares it in <sys/stat.h> but ships no definition
 * (toolchain libc.a exports stat/_stat_r only); sp_cold.c's File helpers
 * call it. The ESP32 VFS has no symlink concept -- stat() is the honest
 * semantics.
 */

#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>         /* struct stat; no socket decls inside */

/* ---- minimal lwip-shaped ABI (see header comment) ---------------------- */
typedef unsigned int   lw_socklen_t;              /* u32_t on RV32 */
typedef unsigned char  lw_sa_family_t;

struct lw_sockaddr {
  unsigned char     sa_len;
  lw_sa_family_t    sa_family;
  char              sa_data[14];
};

struct lw_linger {
  int l_onoff;
  int l_linger;
};

struct lw_addrinfo {
  int                ai_flags;
  int                ai_family;
  int                ai_socktype;
  int                ai_protocol;
  lw_socklen_t       ai_addrlen;
  struct lw_sockaddr *ai_addr;   /* lwip: struct sockaddr * -- same layout */
  char              *ai_canonname;
  struct lw_addrinfo *ai_next;
};

/* ---- lwip entry points (lwip/sockets.h, LWIP_COMPAT_SOCKETS=0) --------- */
extern int      lwip_socket(int domain, int type, int protocol);
extern int      lwip_bind(int s, const struct lw_sockaddr *name, lw_socklen_t namelen);
extern int      lwip_listen(int s, int backlog);
extern int      lwip_accept(int s, struct lw_sockaddr *addr, lw_socklen_t *addrlen);
extern int      lwip_connect(int s, const struct lw_sockaddr *name, lw_socklen_t namelen);
extern int      lwip_shutdown(int s, int how);
extern int      lwip_setsockopt(int s, int level, int optname, const void *optval, lw_socklen_t optlen);
extern int      lwip_getsockopt(int s, int level, int optname, void *optval, lw_socklen_t *optlen);
extern int      lwip_getsockname(int s, struct lw_sockaddr *name, lw_socklen_t *namelen);
extern int      lwip_getpeername(int s, struct lw_sockaddr *name, lw_socklen_t *namelen);
extern long     lwip_send(int s, const void *dataptr, size_t size, int flags);
extern long     lwip_sendto(int s, const void *dataptr, size_t size, int flags,
                            const struct lw_sockaddr *to, lw_socklen_t tolen);
extern long     lwip_recv(int s, void *mem, size_t len, int flags);
extern long     lwip_recvfrom(int s, void *mem, size_t len, int flags,
                              struct lw_sockaddr *from, lw_socklen_t *fromlen);
extern int      lwip_getaddrinfo(const char *nodename, const char *servname,
                                 const struct lw_addrinfo *hints,
                                 struct lw_addrinfo **res);
extern void     lwip_freeaddrinfo(struct lw_addrinfo *ai);

/* ---- the POSIX spellings the RT archive references ---------------------- */

int socket(int domain, int type, int protocol)
{
    return lwip_socket(domain, type, protocol);
}

int bind(int s, const struct lw_sockaddr *name, lw_socklen_t namelen)
{
    return lwip_bind(s, name, namelen);
}

int listen(int s, int backlog)
{
    return lwip_listen(s, backlog);
}

int accept(int s, struct lw_sockaddr *addr, lw_socklen_t *addrlen)
{
    return lwip_accept(s, addr, addrlen);
}

int connect(int s, const struct lw_sockaddr *name, lw_socklen_t namelen)
{
    return lwip_connect(s, name, namelen);
}

int shutdown(int s, int how)
{
    return lwip_shutdown(s, how);
}

int setsockopt(int s, int level, int optname, const void *optval, lw_socklen_t optlen)
{
    return lwip_setsockopt(s, level, optname, optval, optlen);
}

int getsockopt(int s, int level, int optname, void *optval, lw_socklen_t *optlen)
{
    return lwip_getsockopt(s, level, optname, optval, optlen);
}

int getsockname(int s, struct lw_sockaddr *name, lw_socklen_t *namelen)
{
    return lwip_getsockname(s, name, namelen);
}

int getpeername(int s, struct lw_sockaddr *name, lw_socklen_t *namelen)
{
    return lwip_getpeername(s, name, namelen);
}

ssize_t send(int s, const void *dataptr, size_t size, int flags)
{
    return lwip_send(s, dataptr, size, flags);
}

ssize_t sendto(int s, const void *dataptr, size_t size, int flags,
               const struct lw_sockaddr *to, lw_socklen_t tolen)
{
    return lwip_sendto(s, dataptr, size, flags, to, tolen);
}

ssize_t recv(int s, void *mem, size_t len, int flags)
{
    return lwip_recv(s, mem, len, flags);
}

ssize_t recvfrom(int s, void *mem, size_t len, int flags,
                 struct lw_sockaddr *from, lw_socklen_t *fromlen)
{
    return lwip_recvfrom(s, mem, len, flags, from, fromlen);
}

int getaddrinfo(const char *nodename, const char *servname,
                const struct lw_addrinfo *hints, struct lw_addrinfo **res)
{
    return lwip_getaddrinfo(nodename, servname, hints, res);
}

void freeaddrinfo(struct lw_addrinfo *ai)
{
    lwip_freeaddrinfo(ai);
}

int lstat(const char *path, struct stat *buf)
{
    /* No symlinks on the ESP32 VFS; lstat == stat. */
    return stat(path, buf);
}
