/* shim/esp32/include/poll.h -- newlib ships none (ESP-IDF's newlib
 * component adds sys/poll.h, but that one drags in the project's
 * sdkconfig.h, which a standalone RT build does not have).
 *
 * Shapes and values match ESP-IDF's newlib platform_include/sys/poll.h
 * exactly (POLLIN 1<<0 ... POLLNVAL 1<<7, struct pollfd {int,short,short},
 * nfds_t unsigned int) so RT objects agree with the poll() that ESP-IDF's
 * newlib src/poll.c provides at the firmware link.
 *
 * M5a: in the firmware build, mdns.h drags in ESP-IDF's <sys/poll.h>
 * alongside this shim -- both define struct pollfd, and the differing
 * include guards let both through, producing a redefinition error.
 * Defer to IDF's header when it exists on the include path (the two are
 * content-identical); the standalone RT build has no sys/poll.h and
 * falls back to the definitions below.
 *
 * LWIP_SOCKET_POLL: lwip_poll exists but with the sockets component
 * config; the honest answer for an RT-only build is the declaration --
 * the final link decides whether poll resolves to newlib's poll.c or
 * lwip's alias. POLLONESHOT/POLL_CLOEXEC etc. from sp_io.c usage are
 * EPOLL_ names mis-grepped in the survey; the only poll API surface
 * spinel touches is poll()/POLLIN/POLLOUT/POLLERR/POLLHUP/POLLPRI.
 */
#ifndef _SP_SHIM_POLL_H
#define _SP_SHIM_POLL_H

#if defined(__has_include)
#  if __has_include(<sys/poll.h>)
#    include <sys/poll.h>
     /* IDF newlib provided it; nothing else to define. The wrapper's
      * guard keeps downstream double-includes a no-op. */
#    define _SP_SHIM_POLL_DEFERRED 1
#  endif
#endif

#ifndef _SP_SHIM_POLL_DEFERRED

#include <sys/socket.h>    /* socklen_t for symmetry; harmless */

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int nfds_t;

struct pollfd {
  int   fd;        /* file descriptor */
  short events;    /* requested events */
  short revents;  /* returned events */
};

#define POLLIN      (1u << 0)   /* readable without blocking */
#define POLLRDNORM  (1u << 1)
#define POLLRDBAND  (1u << 2)
#define POLLPRI     POLLRDBAND  /* high-priority readable (IDF's trick to fit a byte) */
#define POLLOUT     (1u << 3)   /* writable without blocking */
#define POLLWRNORM  POLLOUT
#define POLLWRBAND (1u << 4)
#define POLLERR     (1u << 5)
#define POLLHUP     (1u << 6)
#define POLLNVAL    (1u << 7)

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* !_SP_SHIM_POLL_DEFERRED */

#endif /* _SP_SHIM_POLL_H */
