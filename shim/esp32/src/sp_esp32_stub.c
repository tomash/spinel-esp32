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
#include <time.h>       /* struct timespec for nanosleep */
#include "ucontext.h"   /* Xtensa link-time plugs (see bottom of file) */

/* FreeRTOS calls, forward-declared: this file is compiled into the RT
 * archive by tools/build-rt.sh, whose include path is the shim + vendor
 * headers only (no ESP-IDF).  The firmware link resolves these against
 * libfreertos.a.  Tick contract hard-coded here matches the firmware's
 * sdkconfig: CONFIG_FREERTOS_HZ=100, 16-bit ticks (ESP-IDF default).
 * portTICK_PERIOD_MS = 1000 / 100 = 10. */
extern void vTaskDelay(uint32_t ticks);
extern uint32_t xTaskGetTickCount(void);


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

/* signal(): plain POSIX name (newlib exports none) -- spinel's exception
 * machinery and the (SPINEL_ALLOC_REPORT-only) GC debug path call it. */
#include <stddef.h>
void (*signal(int sig, void (*handler)(int)))(int) {
    (void)sig; (void)handler; errno = ENOSYS; return NULL;
}
int siginterrupt(int sig, int flag) { (void)sig; (void)flag; errno = ENOSYS; return -1; }

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
long sysconf(int name) { (void)name; errno = ENOSYS; return -1L; }
int getrlimit(int res, void *rl) { (void)res; (void)rl; errno = ENOSYS; return -1; }
int setrlimit(int res, const void *rl) { (void)res; (void)rl; errno = ENOSYS; return -1; }
int getrusage(int who, void *ru) { (void)who; (void)ru; errno = ENOSYS; return -1; }

/* ---- priorities (Process.getpriority, sp_cold.c:990) ---- */
int getpriority(int which, id_t who) { (void)which; (void)who; errno = ENOSYS; return -1; }

/* ---- Kernel#sleep (sp_cold.c sp_sleep -> nanosleep): the one POSIX
 * timer newlib-on-IDF does not provide.  Implemented on vTaskDelay:
 * ticks are 10ms (Hz=100), rounding UP so a sub-tick sleep still
 * yields one tick.  rem is only computed when the caller asks. */
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (req == NULL || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        return -1;
    }
    /* 64-bit ns -> ticks, round up; at most ~4294967 ticks (~49 days). */
    int64_t ns = (int64_t)req->tv_sec * 1000000000L + (int64_t)req->tv_nsec;
    uint32_t ticks = (uint32_t)((ns + 9999999L) / 10000000L); /* ceil(ns/10ms) */
    if (ticks == 0) ticks = 1;
    uint32_t start = xTaskGetTickCount();
    vTaskDelay(ticks);
    if (rem != NULL) {
        uint32_t elapsed = (uint32_t)(xTaskGetTickCount() - start);
        if (elapsed < ticks) {
            int64_t left_ns = (int64_t)(ticks - elapsed) * 10000000L;
            rem->tv_sec  = (time_t)(left_ns / 1000000000L);
            rem->tv_nsec = (long)(left_ns % 1000000000L);
        } else {
            rem->tv_sec = 0; rem->tv_nsec = 0;
        }
    }
    return 0;
}

/* ---- ucontext: link-time plugs for the coroutine fallback (Xtensa only) ----
 * sp_fiber.o/sp_sched.o are always pulled from the archive (the cooperative
 * call sites in sp_io.c/sp_system.c/sp_cold.c reference sp_Thread_pass and
 * sp_sched_wait_child unconditionally; the scheduler's GC hooks reach the
 * fiber code by name), and on Xtensa lib/sp_fiber_ctx.h takes the POSIX
 * ucontext fallback (SP_FIBER_ASM is 0), so getcontext/makecontext/
 * swapcontext/setcontext must exist at link time. RV32 has the real thing
 * (src/ucontext.S); Xtensa's windowed-ABI switch is a later milestone.
 *
 * These definitions are UNREACHABLE, not a degraded path: Fiber.new mmaps
 * its stack, the shim answers MAP_FAILED and sp_fiber.c raises FiberError
 * before any context exists; resume/yield/Enumerator all check f->stack
 * first and raise the same way. Guarded to __xtensa__ so the day the real
 * asm lands (or if these are ever compiled for RV32) there is a duplicate-
 * definition error, not a silent shadow of working code. */
#ifdef __xtensa__
#include <stdlib.h>     /* abort */

int getcontext(ucontext_t *ucp) {
    (void)ucp; errno = ENOSYS;
    abort();            /* unreachable: see above */
}
int setcontext(const ucontext_t *ucp) {
    (void)ucp; errno = ENOSYS;
    abort();            /* unreachable: see above */
}
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...) {
    (void)ucp; (void)func; (void)argc;
    abort();            /* unreachable: see above */
}
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp) {
    (void)oucp; (void)ucp; errno = ENOSYS;
    abort();            /* unreachable: see above */
}
#endif /* __xtensa__ */

#endif /* __ESP32_SHIM__ */
