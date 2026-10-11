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

#endif /* __SPINEL_SHIM_STDIO_H */