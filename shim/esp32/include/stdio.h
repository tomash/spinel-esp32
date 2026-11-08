/*
 * shim/esp32/include/stdio.h -- shadow of newlib's stdio.h (M2)
 *
 * spinel_rt.h inlines sp_File_gets, which calls POSIX getline(). newlib
 * ships the function as __getline/__getdelim (stdio.h:537-538) and does
 * not define the POSIX name as a symbol, so the app TU's call would be
 * an implicit declaration + undefined reference. Shadow newlib's header
 * (this dir is first on -I) and alias the POSIX name onto newlib's own.
 */
#include_next <stdio.h>

#ifndef __SPINEL_SHIM_STDIO_H
#define __SPINEL_SHIM_STDIO_H

/* newlib: ssize_t __getline (char **, size_t *, FILE *); (stdio.h:538) */
#define getline __getline

/* stdout stays line-buffered. Spinel master (820b9a6a6, "STDOUT on a terminal
 * is written through") switches stdout to _IONBF when isatty(1), and the
 * ESP-IDF UART console is a tty. But ESP-IDF's stdout is ONE stream shared by
 * every FreeRTOS task, and newlib prints to an unbuffered stream through
 * __sbprintf, which puts a BUFSIZ (1 KB) buffer on the caller's stack: the
 * first ESP_LOG from the 2304-byte sys_evt task after the Ruby program
 * started overflowed it (Stack protection fault in _vfprintf_r at
 * IP_EVENT_STA_GOT_IP, wifi_status.rb, every boot). Line buffering already
 * gets a finished line to the console; the cost is that an explicit
 * STDOUT.sync = true, and a prompt without a newline, also wait for the
 * newline on this target. Other streams are untouched. */
static inline int sp_esp32_setvbuf(FILE *f, char *buf, int mode, size_t size) {
    if (f == stdout && mode == _IONBF) { mode = _IOLBF; buf = NULL; size = 0; }
    return setvbuf(f, buf, mode, size);
}
#define setvbuf sp_esp32_setvbuf

#endif /* __SPINEL_SHIM_STDIO_H */