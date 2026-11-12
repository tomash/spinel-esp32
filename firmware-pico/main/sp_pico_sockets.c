/*
 * sp_pico_sockets.c -- the POSIX socket surface for the runtime archive on
 * the Pico SDK (the counterpart of firmware/main/sp_lwip_compat.c).
 *
 * The runtime calls socket/bind/accept/... and read/write/close/fcntl/poll
 * on the resulting fds, out of line. lwIP (LWIP_COMPAT_SOCKETS 0) exports
 * only lwip_* names, and there is no VFS here as on ESP-IDF, so this file:
 *
 *   1. defines the socket calls as wrappers onto lwip_*;
 *   2. overrides the SDK's weak newlib syscalls (_read, _write, _close,
 *      _fstat, _isatty) so fds >= LWIP_SOCKET_OFFSET (16) go to lwIP and
 *      0-2 keep the SDK's stdio behaviour;
 *      _open knows one path, /dev/null (fd SP_FD_DEVNULL): the runtime
 *      opens it as the sentinel stream a closed IO handle points at
 *      (sp_io.c sp_io_closed_sentinel), which ESP-IDF's VFS provides;
 *   3. defines fcntl, ioctl and poll outright, translating the values that
 *      differ: the runtime was compiled with newlib's O_NONBLOCK (0x4000)
 *      and shim/esp32/include/poll.h's bits; lwIP uses O_NONBLOCK 1 and its
 *      own POLL* bits.
 *
 * Struct layouts (sockaddr with sa_len, addrinfo) and SOL_/SO_/MSG_ values
 * already agree: the shim headers were written to lwIP's.
 */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "pico/stdio.h"
#include "pico/time.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define SP_FD_IS_SOCK(fd) ((fd) >= LWIP_SOCKET_OFFSET)
#define SP_FD_DEVNULL     (LWIP_SOCKET_OFFSET - 1)

/* ---- 1. socket calls --------------------------------------------------- */
int socket(int d, int t, int p) { return lwip_socket(d, t, p); }
int bind(int s, const struct sockaddr *a, socklen_t l) { return lwip_bind(s, a, l); }
int listen(int s, int b) { return lwip_listen(s, b); }
int accept(int s, struct sockaddr *a, socklen_t *l) { return lwip_accept(s, a, l); }
int connect(int s, const struct sockaddr *a, socklen_t l) { return lwip_connect(s, a, l); }
int shutdown(int s, int how) { return lwip_shutdown(s, how); }
int setsockopt(int s, int lv, int o, const void *v, socklen_t l) { return lwip_setsockopt(s, lv, o, v, l); }
int getsockopt(int s, int lv, int o, void *v, socklen_t *l) { return lwip_getsockopt(s, lv, o, v, l); }
int getsockname(int s, struct sockaddr *a, socklen_t *l) { return lwip_getsockname(s, a, l); }
int getpeername(int s, struct sockaddr *a, socklen_t *l) { return lwip_getpeername(s, a, l); }
ssize_t send(int s, const void *b, size_t n, int f) { return lwip_send(s, b, n, f); }
ssize_t sendto(int s, const void *b, size_t n, int f, const struct sockaddr *a, socklen_t l)
{ return lwip_sendto(s, b, n, f, a, l); }
ssize_t recv(int s, void *b, size_t n, int f) { return lwip_recv(s, b, n, f); }
ssize_t recvfrom(int s, void *b, size_t n, int f, struct sockaddr *a, socklen_t *l)
{ return lwip_recvfrom(s, b, n, f, a, l); }
int getaddrinfo(const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r)
{ return lwip_getaddrinfo(n, s, h, r); }
void freeaddrinfo(struct addrinfo *ai) { lwip_freeaddrinfo(ai); }
struct hostent *gethostbyname(const char *n) { return lwip_gethostbyname(n); }

/* ---- 2. newlib syscalls ------------------------------------------------- */
int _open(const char *path, int flags, ...)
{
    (void)flags;
    if (path && strcmp(path, "/dev/null") == 0) return SP_FD_DEVNULL;
    errno = ENOENT;   /* no file system */
    return -1;
}

int _read(int fd, char *buf, int len)
{
    if (SP_FD_IS_SOCK(fd)) return (int)lwip_read(fd, buf, (size_t)len);
    if (fd == SP_FD_DEVNULL) return 0;
    if (fd == 0) return stdio_get_until(buf, len, at_the_end_of_time);
    errno = EBADF;
    return -1;
}

int _write(int fd, char *buf, int len)
{
    if (SP_FD_IS_SOCK(fd)) return (int)lwip_write(fd, buf, (size_t)len);
    if (fd == 1 || fd == 2) { stdio_put_string(buf, len, false, true); return len; }
    if (fd == SP_FD_DEVNULL) return len;
    errno = EBADF;
    return -1;
}

int _close(int fd)
{
    if (SP_FD_IS_SOCK(fd)) return lwip_close(fd);
    if (fd == SP_FD_DEVNULL) return 0;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof *st);
    st->st_mode = SP_FD_IS_SOCK(fd) ? S_IFSOCK : S_IFCHR;
    return 0;
}

int _isatty(int fd) { return fd >= 0 && fd <= 2; }

/* ---- 3. fcntl / ioctl / poll -------------------------------------------- */
#define SP_O_NONBLOCK 0x4000   /* newlib's, as the runtime was built */

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    int arg = va_arg(ap, int);
    va_end(ap);
    if (!SP_FD_IS_SOCK(fd)) return cmd == F_GETFL ? 2 /* O_RDWR */ : 0;
    switch (cmd) {
    case F_GETFL: {
        int fl = lwip_fcntl(fd, F_GETFL, 0);
        if (fl < 0) return fl;
        return 2 | ((fl & O_NONBLOCK) ? SP_O_NONBLOCK : 0);
    }
    case F_SETFL:
        return lwip_fcntl(fd, F_SETFL, (arg & SP_O_NONBLOCK) ? O_NONBLOCK : 0);
    default:
        return 0;   /* F_GETFD/F_SETFD (close-on-exec): no exec here */
    }
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (!SP_FD_IS_SOCK(fd)) { errno = ENOTTY; return -1; }
    return lwip_ioctl(fd, (long)req, arg);
}

/* shim/esp32/include/poll.h's bits, as the runtime was compiled */
#define SP_POLLIN   (1u << 0)
#define SP_POLLPRI  (1u << 2)
#define SP_POLLOUT  (1u << 3)
#define SP_POLLERR  (1u << 5)
#define SP_POLLHUP  (1u << 6)
#define SP_POLLNVAL (1u << 7)

struct sp_pollfd { int fd; short events; short revents; };

static short sp_poll_to_lwip(short ev)
{
    short r = 0;
    if (ev & SP_POLLIN)  r |= POLLIN;
    if (ev & SP_POLLPRI) r |= POLLPRI;
    if (ev & SP_POLLOUT) r |= POLLOUT;
    return r;
}

static short sp_poll_from_lwip(short ev)
{
    short r = 0;
    if (ev & (POLLIN | POLLRDNORM)) r |= SP_POLLIN;
    if (ev & POLLPRI)  r |= SP_POLLPRI;
    if (ev & (POLLOUT | POLLWRNORM)) r |= SP_POLLOUT;
    if (ev & POLLERR)  r |= SP_POLLERR;
    if (ev & POLLHUP)  r |= SP_POLLHUP;
    if (ev & POLLNVAL) r |= SP_POLLNVAL;
    return r;
}

#define SP_POLL_MAX 16

int poll(struct sp_pollfd *fds, unsigned int nfds, int timeout)
{
    if (nfds == 0) {
        if (timeout > 0) vTaskDelay(pdMS_TO_TICKS(timeout) ? pdMS_TO_TICKS(timeout) : 1);
        return 0;
    }
    if (nfds > SP_POLL_MAX) { errno = EINVAL; return -1; }
    /* Console fds answer at once: stdout/stderr are always writable, stdin
     * is never reported readable (no line discipline to wake on). Sockets
     * go to lwip_poll, without blocking if a console fd is already ready. */
    struct pollfd lw[SP_POLL_MAX];
    int map[SP_POLL_MAX];
    int nsock = 0, ready = 0;
    for (unsigned int i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0) continue;
        if (SP_FD_IS_SOCK(fds[i].fd)) {
            lw[nsock].fd = fds[i].fd;
            lw[nsock].events = sp_poll_to_lwip(fds[i].events);
            lw[nsock].revents = 0;
            map[nsock++] = (int)i;
        } else if (fds[i].fd == 1 || fds[i].fd == 2) {
            fds[i].revents = fds[i].events & SP_POLLOUT;
        } else if (fds[i].fd == SP_FD_DEVNULL) {
            fds[i].revents = fds[i].events & (SP_POLLIN | SP_POLLOUT);
        } else if (fds[i].fd != 0) {
            fds[i].revents = SP_POLLNVAL;
        }
        if (fds[i].revents) ready++;
    }
    if (nsock == 0) {
        if (ready == 0 && timeout != 0)
            vTaskDelay(timeout < 0 ? portMAX_DELAY : (pdMS_TO_TICKS(timeout) ? pdMS_TO_TICKS(timeout) : 1));
        return ready;
    }
    int n = lwip_poll(lw, (nfds_t)nsock, ready ? 0 : timeout);
    if (n < 0) return ready ? ready : n;
    for (int k = 0; k < nsock; k++) {
        short r = sp_poll_from_lwip(lw[k].revents);
        if (r) { fds[map[k]].revents = r; ready++; }
    }
    return ready;
}
