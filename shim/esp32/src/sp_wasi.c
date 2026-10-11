/* wasm32-wasi: the POSIX calls wasi-libc (preview 1) does not implement,
   defined to fail the way an unsupported call fails on any POSIX system:
   -1 with errno set, NULL, or EAI_FAIL. WASI has no processes, no signals,
   no name resolution, no user database, no terminals and no stack
   switching; a program that reaches for them gets the Errno the runtime
   already raises for that answer (Errno::ENOSYS for fork, Errno::ENOTSUP
   for a socket, SocketError for a name), not a link error, so every
   program links and only the one that spawns a process finds out at run
   time. The headers beside this file declare what wasi-libc's leave out.
   Only a wasi build compiles this file (see the Makefile's wasm target). */
#ifdef __wasi__
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <netdb.h>
#include <pwd.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/file.h>

#define SP_WASI_NOSYS(...) { (void)(__VA_ARGS__); errno = ENOSYS; return -1; }

/* processes */
pid_t fork(void) { errno = ENOSYS; return -1; }
int execvp(const char *f, char *const a[]) SP_WASI_NOSYS(f, a)
int execv(const char *f, char *const a[]) SP_WASI_NOSYS(f, a)
int execl(const char *p, const char *a, ...) SP_WASI_NOSYS(p, a)
pid_t waitpid(pid_t p, int *st, int fl) { (void)p; (void)st; (void)fl; errno = ECHILD; return -1; }
pid_t wait(int *st) { (void)st; errno = ECHILD; return -1; }
FILE *popen(const char *c, const char *m) { (void)c; (void)m; errno = ENOSYS; return NULL; }
int pclose(FILE *f) SP_WASI_NOSYS(f)
int kill(pid_t p, int s) SP_WASI_NOSYS(p, s)
int setpgid(pid_t p, pid_t g) SP_WASI_NOSYS(p, g)
int getpriority(int w, id_t who) { (void)w; (void)who; return 0; }
int setpriority(int w, id_t who, int p) { (void)w; (void)who; (void)p; return 0; }
int getrlimit(int r, struct rlimit *l) SP_WASI_NOSYS(r, l)
int setrlimit(int r, const struct rlimit *l) SP_WASI_NOSYS(r, l)

/* signals: sigaction fails, so a handler is never installed and the
   runtime's fallbacks (no fault report, no alloc-report signal, no SIGTERM
   accept-loop exit) apply. */
int sigaction(int s, const struct sigaction *a, struct sigaction *o) SP_WASI_NOSYS(s, a, o)
int sigaltstack(const stack_t *s, stack_t *o) SP_WASI_NOSYS(s, o)
int sigemptyset(sigset_t *s) { (void)s; return 0; }
int sigaddset(sigset_t *s, int n) { (void)s; (void)n; return 0; }
int sigprocmask(int h, const sigset_t *s, sigset_t *o) { (void)h; (void)s; (void)o; return 0; }

/* stack switching: getcontext fails, so Fiber.new reports its stack */
int getcontext(ucontext_t *u) SP_WASI_NOSYS(u)
int setcontext(const ucontext_t *u) SP_WASI_NOSYS(u)
void makecontext(ucontext_t *u, void (*f)(void), int n, ...) { (void)u; (void)f; (void)n; }
int swapcontext(ucontext_t *o, const ucontext_t *u) SP_WASI_NOSYS(o, u)

/* sockets: preview 1 can accept on a pre-opened listener but cannot open
   one; the runtime's Errno mapping turns ENOTSUP into the exception */
int socket(int d, int t, int p) { (void)d; (void)t; (void)p; errno = ENOTSUP; return -1; }
int socketpair(int d, int t, int p, int sv[2]) { (void)d; (void)t; (void)p; (void)sv; errno = ENOTSUP; return -1; }
int bind(int fd, const struct sockaddr *a, socklen_t l) { (void)fd; (void)a; (void)l; errno = ENOTSUP; return -1; }
int listen(int fd, int b) { (void)fd; (void)b; errno = ENOTSUP; return -1; }
int connect(int fd, const struct sockaddr *a, socklen_t l) { (void)fd; (void)a; (void)l; errno = ENOTSUP; return -1; }
int setsockopt(int fd, int lv, int o, const void *v, socklen_t l) { (void)fd; (void)lv; (void)o; (void)v; (void)l; errno = ENOTSUP; return -1; }
int getsockname(int fd, struct sockaddr *a, socklen_t *l) { (void)fd; (void)a; (void)l; errno = ENOTSUP; return -1; }
int getpeername(int fd, struct sockaddr *a, socklen_t *l) { (void)fd; (void)a; (void)l; errno = ENOTSUP; return -1; }
ssize_t sendto(int fd, const void *b, size_t n, int fl, const struct sockaddr *a, socklen_t l) { (void)fd; (void)b; (void)n; (void)fl; (void)a; (void)l; errno = ENOTSUP; return -1; }
ssize_t recvfrom(int fd, void *b, size_t n, int fl, struct sockaddr *a, socklen_t *l) { (void)fd; (void)b; (void)n; (void)fl; (void)a; (void)l; errno = ENOTSUP; return -1; }
int getaddrinfo(const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r) { (void)n; (void)s; (void)h; (void)r; return EAI_FAIL; }
void freeaddrinfo(struct addrinfo *a) { (void)a; }
int getnameinfo(const struct sockaddr *a, socklen_t al, char *h, socklen_t hl, char *s, socklen_t sl, int f) { (void)a; (void)al; (void)h; (void)hl; (void)s; (void)sl; (void)f; return EAI_FAIL; }
const char *gai_strerror(int e) { (void)e; return "name resolution is not available on wasm32-wasi"; }

/* users, files, terminals */
struct passwd *getpwnam(const char *n) { (void)n; errno = ENOSYS; return NULL; }
struct passwd *getpwuid(uid_t u) { (void)u; errno = ENOSYS; return NULL; }
uid_t getuid(void) { return 0; }
gid_t getgid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getegid(void) { return 0; }
pid_t getppid(void) { return 1; }
int getgroups(int n, gid_t *g) { (void)n; (void)g; return 0; }
mode_t umask(mode_t m) { (void)m; return 022; }
int chown(const char *p, uid_t u, gid_t g) SP_WASI_NOSYS(p, u, g)
int mkfifo(const char *p, mode_t m) SP_WASI_NOSYS(p, m)
int fchdir(int fd) SP_WASI_NOSYS(fd)
int flock(int fd, int op) { (void)fd; (void)op; return 0; }   /* one process: the lock is always free */
int madvise(void *a, size_t n, int adv) { (void)a; (void)n; (void)adv; return 0; }
#endif
