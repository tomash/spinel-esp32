/* shim/esp32/include/sys/resource.h -- newlib ships one, but with only
 * getrusage/RUSAGE_* (no rlim_t, no struct rlimit, no RLIMIT_ or PRIO_
 * no setrlimit/getpriority). sp_process.c uses rlim_t + RLIMIT_CPU +
 * RLIMIT_AS + setrlimit (fork path, and the rlimit opts); sp_cold.c uses
 * getpriority + id_t. This shim keeps newlib's rusage surface (getrusage
 * IS in libc) and adds the rest as declarations that resolve to the
 * ENOSYS stubs in src/sp_esp32_stub.c at link time.
 *
 * Process.spawn with rlimit opts therefore compiles; at runtime setrlimit
 * answers -1/ENOSYS which the spawn path reports (sp_process_spawn_fail
 * wraps errno) -- honest failure, no false limits.
 */
#ifndef _SP_SHIM_SYS_RESOURCE_H
#define _SP_SHIM_SYS_RESOURCE_H

#include <sys/types.h>      /* id_t */
#include <sys/time.h>       /* struct timeval (newlib's own rusage needs it */

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF     0    /* calling process */
#define RUSAGE_CHILDREN -1   /* terminated child processes */

struct rusage {
  struct timeval ru_utime;   /* user time used */
  struct timeval ru_stime;   /* system time used */
};

/* ---- rlimits (Linux/generic values) ------------------------------------ */
typedef unsigned long rlim_t;

#define RLIM_INFINITY  ((rlim_t)(~0UL >> 1))

#define RLIMIT_CPU     0     /* CPU time in seconds */
#define RLIMIT_FSIZE   1     /* Maximum filesize */
#define RLIMIT_DATA    2     /* max data size */
#define RLIMIT_STACK   3     /* max stack size */
#define RLIMIT_CORE    4     /* max core file size */
#define RLIMIT_RSS     5     /* max resident set size */
#define RLIMIT_NPROC   6     /* max number of processes */
#define RLIMIT_NOFILE  7     /* max number of open files */
#define RLIMIT_MEMLOCK 8     /* max locked-in-memory address space */
#define RLIMIT_AS      9     /* address space limit */

struct rlimit {
  rlim_t rlim_cur;           /* soft limit */
  rlim_t rlim_max;           /* hard limit */
};

/* ---- priorities (Process.getpriority) ----------------------------------- */
#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2

/* ---- declarations -------------------------------------------------------- */
int getrusage (int, struct rusage *);          /* newlib's libc has this */
int getrlimit (int, struct rlimit *);          /* stub: ENOSYS */
int setrlimit (int, const struct rlimit *);    /* stub: ENOSYS */
int getpriority (int which, id_t who);          /* stub: ENOSYS */

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_SYS_RESOURCE_H */
