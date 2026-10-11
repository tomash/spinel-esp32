/* shim/esp32/src/sp_esp32_stub.c -- the POSIX surface newlib/FreeRTOS lacks,
 * defined to fail the way an unsupported call fails on any POSIX system
 * (the lib/wasi/sp_wasi.c precedent), plus the two ESP32-specific behaviors:
 *
 *   - mmap/mprotect/munmap answer MAP_FAILED/-1, which the runtime handles:
 *     the main stack runs on the FreeRTOS task stack (its designed fallback),
 *     Fiber.new raises FiberError honestly, the iobuffer fast path falls back
 *     to malloc. See include/sys/mman.h for the source evidence.
 *   - signals answer -1/ENOSYS: sp_fiber_fault_arm tolerates every failure
 *     (its sigaction calls are all guarded, sp_fiber.c:396-421), so the
 *     SIGSEGV guard page simply never arms; the alloc-report path only arms
 *     under SPINEL_ALLOC_REPORT and tolerates failure to install.
 *   - processes: FreeRTOS has none (the wasi precedent). fork answers -1 so
 *     system(`/backtick`) report failure through their designed paths
 *     (sp_last_status = -1 -> nil $?).
 *
 * libc.a exports fork/kill/raise already; our definitions take precedence
 * only when the linker pulls this object first (build-rt.sh archives it
 * after the members, and the app link order puts libspinel_rt_esp32.a
 * before libc -- so libc's members are never searched for these symbols;
 * no duplicate-definition conflict can occur).
 *
 * Only a build with the shim in scope compiles this file
 * (tools/build-rt.sh adds -D__ESP32_SHIM__; an accidental host build of it
 * must not shadow the host's libc).
 */
#ifdef __ESP32_SHIM__

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <signal.h>     /* the shim's own: struct sigaction, stack_t */

/* ---- sys/mman.h ---- */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off;
    errno = ENOMEM;
    return (void *)-1;   /* MAP_FAILED: every caller has a designed fallback */
}
int munmap(void *addr, size_t len) { (void)addr; (void)len; errno = EINVAL; return -1; }
int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; errno = EACCES; return -1; }
int madvise(void *addr, size_t len, int advice) { (void)addr; (void)len; (void)advice; errno = ENOSYS; return -1; }

/* ---- signals: no asynchronous events on FreeRTOS ---- */
int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    (void)sig; (void)act; (void)old; errno = ENOSYS; return -1;
}
int sigaltstack(const stack_t *ss, stack_t *oss) { (void)ss; (void)oss; errno = ENOSYS; return -1; }
int sigprocmask(int how, const sigset_t *set, sigset_t *old) { (void)how; (void)set; (void)old; errno = ENOSYS; return -1; }

/* ---- processes: FreeRTOS has none (the wasi precedent) ---- */
int kill(pid_t p, int s) { (void)p; (void)s; errno = ENOSYS; return -1; }
pid_t fork(void) { errno = ENOSYS; return (pid_t)-1; }
int execvp(const char *f, char *const a[]) { (void)f; (void)a; errno = ENOSYS; return -1; }
int execv(const char *f, char *const a[]) { (void)f; (void)a; errno = ENOSYS; return -1; }
int execl(const char *path, const char *arg0, ...) {
    (void)path; (void)arg0;
    errno = ENOSYS; return -1;
}
pid_t waitpid(pid_t p, int *st, int fl) { (void)p; (void)st; (void)fl; errno = ECHILD; return (pid_t)-1; }

/* ---- resource limits: report "unlimited" ---- */
int getrlimit(int res, void *rl) { (void)res; (void)rl; errno = ENOSYS; return -1; }
int setrlimit(int res, const void *rl) { (void)res; (void)rl; errno = ENOSYS; return -1; }
int getrusage(int who, void *ru) { (void)who; (void)ru; errno = ENOSYS; return -1; }

/* ---- priorities (Process.getpriority, sp_cold.c:990) ---- */
int getpriority(int which, id_t who) { (void)which; (void)who; errno = ENOSYS; return -1; }

#endif /* __ESP32_SHIM__ */
