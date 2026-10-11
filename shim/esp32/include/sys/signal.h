/* shim/esp32/include/sys/signal.h -- shadow of newlib's <sys/signal.h>.
 *
 * newlib's <sys/signal.h> is reached only through newlib's own <signal.h>
 * (verified: nothing else in the toolchain's include tree includes it
 * directly), and that <signal.h> is shadowed by shim/esp32/include/signal.h
 * -- so this file never parses in a shim-scope build. It exists as
 * insurance: if a future toolchain header ever pulls <sys/signal.h> in
 * directly, it must not redefine the struct sigaction the RT already saw
 * (newlib's has no sa_sigaction member). So it just delegates.
 */
#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <signal.h>

#endif /* _SYS_SIGNAL_H */
