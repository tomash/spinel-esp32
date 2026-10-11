/* shim/esp32/include/sp_esp32_signal.h -- the signal surface newlib's
 * <signal.h> leaves out, declared so lib/sp_fiber.c and lib/sp_alloc.c
 * compile unchanged (the lib/wasi/signal.h precedent).
 *
 * ESP-IDF's newlib signal.h is minimal: no sigaction, no sigaltstack, no
 * SA_* flags, no siginfo_t. FreeRTOS has no asynchronous signal delivery,
 * so the honest definitions (in src/sp_esp32_stub.c) answer ENOSYS. The
 * runtime tolerates every one of these failures:
 *
 *   - sp_fiber_fault_arm (sp_fiber.c:396): each sigaction install is guarded
 *     by "!= 0 continue", each sigaltstack by an `if (as)`; none arms.
 *   - SPINEL_ALLOC_REPORT boot (sp_alloc.c:1521): guarded installs; the
 *     feature only arms when asked for via env var (never set on target).
 *
 * This header is force-included (-include) ONLY for the runtime compile
 * (tools/build-rt.sh); it must not leak into the app or ESP-IDF TUs.
 */
#ifndef SP_SHIM_ESP32_SIGNAL_H
#define SP_SHIM_ESP32_SIGNAL_H

#include <signal.h>          /* newlib's: SIGSEGV/SIGBUS/... and sig_atomic_t */
#include <sys/types.h>

#define SA_RESTART   0x10000000
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

typedef struct {
  int si_signo, si_errno, si_code;
  void *si_addr;
} siginfo_t;

struct sigaction {
  union {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
  } __sa_handler;
  sigset_t sa_mask;
  int sa_flags;
};
#define sa_handler   __sa_handler.sa_handler
#define sa_sigaction __sa_handler.sa_sigaction

typedef struct sigaltstack { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
int sigaction(int, const struct sigaction *, struct sigaction *);
int sigaltstack(const stack_t *, stack_t *);

#endif /* SP_SHIM_ESP32_SIGNAL_H */
