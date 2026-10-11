/* shim/esp32/include/stdio_ext.h -- newlib HAS <stdio_ext.h> (__fbufsize,
 * __freading, __fpending... as _ELIDABLE_INLINE over the FILE struct) but
 * NOT __freadahead, which sp_io.c:187,1148 uses (its musl branch). This
 * shim shadows the newlib header and adds the missing one as the same kind
 * of inline: bytes already sitting in the FILE's read buffer = _r.
 *
 * It re-declares newlib's own family too, so a TU that includes only this
 * shim gets the full surface (identical semantics -- they read the same
 * __sFILE fields).
 */
#ifndef _SP_SHIM_STDIO_EXT_H
#define _SP_SHIM_STDIO_EXT_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSETLOCKING_QUERY      0
#define FSETLOCKING_INTERNAL   1
#define FSETLOCKING_BYCALLER   2

/* newlib's own inlines, spelled out (they are _ELIDABLE_INLINE in the
 * original header, so re-declaring them here cannot collide) */
void  __fpurge(FILE *);
int   __fsetlocking(FILE *, int);

/* The one newlib does not have: pending stdio read-buffer bytes (musl's
 * __freadahead). _r is "read space left" in newlib's __sFILE -- exactly
 * the count of bytes already buffered and readable without a syscall. */
static __inline size_t __freadahead(FILE *__fp) { return (size_t)(__fp->_r > 0 ? __fp->_r : 0); }

/* the rest of newlib's family, same fields it reads */
static __inline size_t __fbufsize (FILE *__fp) { return (size_t)__fp->_bf._size; }
static __inline int    __freading (FILE *__fp) { return (__fp->_flags & __SRD) != 0; }
static __inline int    __fwriting (FILE *__fp) { return (__fp->_flags & __SWR) != 0; }
static __inline int    __freadable(FILE *__fp) { return (__fp->_flags & (__SRD | __SRW)) != 0; }
static __inline int    __fwritable(FILE *__fp) { return (__fp->_flags & (__SWR | __SRW)) != 0; }
static __inline int    __flbf     (FILE *__fp) { return (__fp->_flags & __SLBF) != 0; }
static __inline size_t __fpending (FILE *__fp) { return (size_t)(__fp->_p - __fp->_bf._base); }

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_STDIO_EXT_H */
