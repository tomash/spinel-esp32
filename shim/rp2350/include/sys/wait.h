/* shim/rp2350/include/sys/wait.h -- newlib's, plus WCOREDUMP, which the
 * Pico newlib leaves out (sp_process_status.c reports it; with no processes
 * on the chip every status is an exit, so it is always 0). */
#pragma once
#include_next <sys/wait.h>
#ifndef WCOREDUMP
#define WCOREDUMP(w) 0
#endif
