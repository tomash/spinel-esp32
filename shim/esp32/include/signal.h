/* shim/esp32/include/signal.h -- full replacement for newlib's <signal.h>.
 *
 * Why a replacement and not an -include add-on: newlib's non-RTEMS
 * struct sigaction has no sa_sigaction member, and sp_fiber.c:413-417 reads
 * cur.sa_handler / writes sa.sa_sigaction directly. A full header is the
 * lib/wasi precedent: the shim directory is first on the include path, so
 * every <signal.h> -- including the ones newlib's own <time.h> and
 * <spawn.h> make -- resolves here. newlib's <sys/signal.h> is only ever
 * included by newlib's <signal.h> (verified by grep), which never parses;
 * shim/esp32/include/sys/signal.h exists as insurance anyway.
 *
 * What is real and what is not:
 *   - SIG numbers: newlib's generic set verbatim (SIGHUP 1 .. SIGUSR2 31,
 *     NSIG 32) so nothing can disagree with the rest of newlib.
 *   - The sigemptyset family: macros, exactly as newlib does it, so there
 *     is nothing to link.
 *   - sigaction/sigaltstack/sigprocmask: declared here, defined in
 *     src/sp_esp32_stub.c to answer -1/ENOSYS. FreeRTOS has no asynchronous
 *     signal delivery; every spinel caller tolerates the failure (the
 *     sigaltstack and sigaction installs in sp_fiber.c:396-421 are guarded,
 *     the alloc-report arm is behind an env var that is never set on target).
 *   - signal()/raise()/kill(): declared, provided by newlib's libc.
 *
 * This header is in scope only for the runtime cross-build
 * (tools/build-rt.sh puts the shim dir first); it must not leak into
 * ESP-IDF TUs or the app.
 */
#ifndef _SIGNAL_H_          /* newlib's guard: its <signal.h> must not re-parse */
#define _SIGNAL_H_

#include <sys/types.h>       /* pid_t, size_t */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;

/* newlib's internal spelling; reused so TUs that saw the real header agree */
typedef void (*_sig_func_ptr)(int);

#define SIG_DFL ((_sig_func_ptr)0)
#define SIG_IGN ((_sig_func_ptr)1)
#define SIG_ERR ((_sig_func_ptr)-1)

/* ---- sigset_t and friends (newlib shapes) ----------------------------- */
#ifndef _SIGSET_T_DECLARED
#define _SIGSET_T_DECLARED
typedef unsigned long sigset_t;
#endif

#define SIG_SETMASK 0        /* set mask with sigprocmask() */
#define SIG_BLOCK   1
#define SIG_UNBLOCK 2

/* ---- signal numbers: newlib's generic (__rtems__-off) set ------------- */
#define SIGHUP    1           /* hangup */
#define SIGINT    2           /* interrupt */
#define SIGQUIT   3           /* quit */
#define SIGILL    4           /* illegal instruction (not reset when caught) */
#define SIGTRAP   5           /* trace trap (not reset when caught) */
#define SIGIOT    6           /* IOT instruction */
#define SIGABRT   6           /* used by abort, replace SIGIOT in the future */
#define SIGEMT    7           /* EMT instruction */
#define SIGFPE    8           /* floating point exception */
#define SIGKILL   9           /* kill (cannot be caught or ignored) */
#define SIGBUS    10          /* bus error */
#define SIGSEGV   11          /* segmentation violation */
#define SIGSYS    12          /* bad argument to system call */
#define SIGPIPE   13          /* write on a pipe with no one to read it */
#define SIGALRM   14          /* alarm clock */
#define SIGTERM   15          /* software termination signal from kill */
#define SIGURG    16          /* urgent condition on IO channel */
#define SIGSTOP   17          /* sendable stop signal not from tty */
#define SIGTSTP   18          /* stop signal from tty */
#define SIGCONT   19          /* continue a stopped process */
#define SIGCHLD   20          /* to parent on child stop or exit */
#define SIGCLD    20          /* System V name for SIGCHLD */
#define SIGTTIN   21          /* to readers pgrp upon background tty read */
#define SIGTTOU   22          /* like TTIN for output if (tp->t_local&LTOSTOP) */
#define SIGIO     23          /* input/output possible signal */
#define SIGPOLL   SIGIO       /* System V name for SIGIO */
#define SIGXCPU   24          /* exceeded CPU time limit */
#define SIGXFSZ   25          /* exceeded file size limit */
#define SIGVTALRM 26          /* virtual time alarm */
#define SIGPROF   27          /* profiling time alarm */
#define SIGWINCH  28          /* window changed */
#define SIGLOST   29          /* resource lost (eg, record-lock lost) */
#define SIGUSR1   30          /* user defined signal 1 */
#define SIGUSR2   31          /* user defined signal 2 */
#define NSIG      32          /* signal 0 implied */

/* Real-time signals: newlib (non-rtems) does not define them; the value is
   arbitrary because every sigaction install fails (see the stub). spinel
   mentions SIGRTMIN only in diagnostics/comments. */
#define SIGRTMIN  32

/* ---- siginfo_t (minimal: spinel reads si_addr only, sp_fiber.c:297) ---- */
typedef struct {
  int  si_signo;             /* signal number */
  int  si_errno;             /* if nonzero, errno value associated with signal */
  int  si_code;              /* signal code */
  void *si_addr;             /* address of faulting instruction or datum */
} siginfo_t;

/* ---- sigevent (newlib's <time.h> timer_create needs the type) ---------- */
union sigval {
  int   sival_int;           /* Integer signal value */
  void *sival_ptr;           /* Pointer signal value */
};

struct sigevent {
  int          sigev_notify; /* Notification type */
  int          sigev_signo; /* Signal number */
  union sigval sigev_value; /* Signal value */
  void       (*sigev_notify_function)(union sigval);
                              /* Notification function (no pthread_attr_t
                                 member: nothing here needs it) */
};

/* ---- sigaction --------------------------------------------------------- */
/* glibc's shape: one union so sa_handler and sa_sigaction overlap, the two
   accessors are macros. sp_fiber.c reads cur.sa_handler when !(sa_flags &
   SA_SIGINFO) and cur.sa_sigaction when set -- both spellings must name the
   same storage. */
#define SA_NOCLDSTOP 0x1      /* Do not generate SIGCHLD when children stop */
#define SA_NOCLDWAIT 0x2      /* Not set by spinel; glibc value */
#define SA_SIGINFO   0x4
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000
#define SA_RESTORER  0x04000000  /* glibc internal; unused here */

struct sigaction {
  union {
    _sig_func_ptr sa_handler;                             /* SIG_DFL/SIG_IGN/fn */
    void (*sa_sigaction)(int, siginfo_t *, void *);       /* SA_SIGINFO arm */
  } __sa_union;
  sigset_t sa_mask;          /* signals to block during the handler */
  int      sa_flags;         /* SA_* flags */
};
#define sa_handler   __sa_union.sa_handler
#define sa_sigaction __sa_union.sa_sigaction

/* ---- alternate signal stack -------------------------------------------- */
typedef struct sigaltstack {
  void  *ss_sp;              /* stack base */
  int    ss_flags;           /* SS_DISABLE or 0 */
  size_t ss_size;            /* stack size */
} stack_t;
#define SS_DISABLE 2

/* ---- set operations as macros (newlib's own trick: nothing to link) ----- */
#define sigaddset(what,sig) (*(what) |=  (unsigned long)(1UL<<(sig)), 0)
#define sigdelset(what,sig) (*(what) &= ~(unsigned long)(1UL<<(sig)), 0)
#define sigemptyset(what)   (*(what) = 0UL, 0)
#define sigfillset(what)    (*(what) = ~(0UL), 0)
#define sigismember(what,sig) (((*(what)) & (1UL<<(sig))) != 0)

/* ---- declarations ------------------------------------------------------- */
_sig_func_ptr signal(int, _sig_func_ptr);
int           raise(int);
int           kill(pid_t, int);
int           sigaction(int, const struct sigaction *, struct sigaction *);
int           sigaltstack(const stack_t *, stack_t *);
int           sigprocmask(int, const sigset_t *, sigset_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_H_ */
