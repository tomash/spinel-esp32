/* shim/rp2350/include/time.h -- newlib's <time.h> with the POSIX clocks on.
 *
 * ESP-IDF's newlib declares clock_gettime/CLOCK_MONOTONIC unconditionally;
 * the Pico toolchain's newlib only under _POSIX_TIMERS/_POSIX_MONOTONIC_CLOCK.
 * The runtime uses both (sp_time, sp_gc, sp_sched...). They are implemented
 * on the RP2350's 64-bit microsecond timer in src/sp_rp2350_stub.c. */
#ifndef SP_RP2350_TIME_H
#define SP_RP2350_TIME_H
#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS 1
#endif
#ifndef _POSIX_MONOTONIC_CLOCK
#define _POSIX_MONOTONIC_CLOCK 200112L
#endif
#include_next <time.h>
int nanosleep(const struct timespec *req, struct timespec *rem);
#endif
