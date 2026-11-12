/* baremetal/tinystdio.c -- the slice of stdio the spinel runtime calls, for
 * MCUs with no file system (the mruby/c console.c idea).
 *
 * Linked before newlib, these definitions win, and newlib's stdio (FILE
 * tables, vfprintf, fvwrite, fflush, the exit-time stream cleanup) stays out
 * of the image. Every stream is the board's console: bytes go to
 * board_putchar(), which a board file overrides (a UART, USB-CDC); the weak
 * default drops them.
 *
 * Formatting is integer-only: %d %i %u %x %X %o %p %c %s %%, with the flags
 * '-' and '0', a field width, a precision for %s, and the length modifiers
 * hh h l ll z j t. Floating-point conversions print "?" -- the runtime
 * formats Floats itself (sp_dtoa), not through printf.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

__attribute__((weak)) void board_putchar(int c) { (void)c; }

/* ---- the formatter: writes through an emit callback ------------------- */
typedef struct { char *buf; size_t cap, len; } tiny_out;   /* buf NULL: console */

static void emit(tiny_out *o, char c) {
  if (o->buf) { if (o->len + 1 < o->cap) o->buf[o->len] = c; }
  else board_putchar((unsigned char)c);
  o->len++;
}

static void emit_pad(tiny_out *o, char c, int n) { while (n-- > 0) emit(o, c); }

static int tiny_vformat(tiny_out *o, const char *f, va_list ap) {
  for (; *f; f++) {
    if (*f != '%') { emit(o, *f); continue; }
    int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
    for (;; f++) {
      if (f[1] == '-') left = 1;
      else if (f[1] == '0') zero = 1;
      else if (f[1] == ' ' || f[1] == '+' || f[1] == '#') { }
      else break;
    }
    f++;
    if (*f == '*') { width = va_arg(ap, int); f++; }
    else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    if (*f == '.') {
      f++; prec = 0;
      if (*f == '*') { prec = va_arg(ap, int); f++; }
      else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
    }
    for (;; f++) {                                   /* length modifiers */
      if (*f == 'l') lng++;
      else if (*f == 'z' || *f == 'j' || *f == 't') lng = (sizeof(size_t) > sizeof(long)) ? 2 : 1;
      else if (*f == 'h') { }
      else break;
    }
    char tmp[24], *s = tmp; int n = 0, neg = 0;
    switch (*f) {
    case 'c': tmp[0] = (char)va_arg(ap, int); n = 1; break;
    case 's':
      s = va_arg(ap, char *); if (!s) s = "(null)";
      n = (int)strlen(s); if (prec >= 0 && n > prec) n = prec;
      zero = 0; break;
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
      unsigned long long v; unsigned base = 10;
      if (*f == 'p') { v = (uintptr_t)va_arg(ap, void *); base = 16; }
      else if (*f == 'd' || *f == 'i') {
        long long sv = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
        if (sv < 0) { neg = 1; v = 0ull - (unsigned long long)sv; } else v = (unsigned long long)sv;
      } else {
        v = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
        base = (*f == 'o') ? 8 : (*f == 'u') ? 10 : 16;
      }
      const char *dig = (*f == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
      char *e = tmp + sizeof tmp; s = e;
      /* 32-bit chunks: keeps a 64-bit divide (libgcc __udivdi3) out of the
         common case on a 32-bit core */
      if (v >> 32) { do { *--s = dig[v % base]; v /= base; } while (v); }
      else { uint32_t w = (uint32_t)v; do { *--s = dig[w % base]; w /= base; } while (w); }
      if (*f == 'p') { *--s = 'x'; *--s = '0'; }
      n = (int)(e - s);
      break;
    }
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
      (void)va_arg(ap, double); tmp[0] = '?'; n = 1; zero = 0; break;
    case '%': tmp[0] = '%'; n = 1; break;
    case '\0': f--; continue;
    default: tmp[0] = '%'; tmp[1] = *f; n = 2; break;
    }
    int pad = width - n - neg;
    if (!left && !zero) emit_pad(o, ' ', pad);
    if (neg) emit(o, '-');
    if (!left && zero) emit_pad(o, '0', pad);
    for (int i = 0; i < n; i++) emit(o, s[i]);
    if (left) emit_pad(o, ' ', pad);
  }
  return (int)o->len;
}

/* ---- console streams --------------------------------------------------- */
int vfprintf(FILE *fp, const char *f, va_list ap) { (void)fp; tiny_out o = {0}; return tiny_vformat(&o, f, ap); }
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int fprintf(FILE *fp, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(fp, f, ap); va_end(ap); return r; }
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vfprintf(stdout, f, ap); va_end(ap); return r; }

int vsnprintf(char *buf, size_t cap, const char *f, va_list ap) {
  tiny_out o = { buf, cap, 0 };
  int r = tiny_vformat(&o, f, ap);
  if (cap) buf[o.len < cap ? o.len : cap - 1] = '\0';
  return r;
}
int snprintf(char *buf, size_t cap, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(buf, cap, f, ap); va_end(ap); return r; }
int vsprintf(char *buf, const char *f, va_list ap) { return vsnprintf(buf, (size_t)-1 / 2, f, ap); }
int sprintf(char *buf, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsprintf(buf, f, ap); va_end(ap); return r; }

/* newlib's integer-only aliases, which GCC substitutes for the calls above
   when a format string has no floating-point conversion */
int fiprintf(FILE *fp, const char *f, ...) __attribute__((alias("fprintf")));
int iprintf(const char *f, ...) __attribute__((alias("printf")));
int vfiprintf(FILE *fp, const char *f, va_list ap) __attribute__((alias("vfprintf")));
int sniprintf(char *b, size_t c, const char *f, ...) __attribute__((alias("snprintf")));
int vsniprintf(char *b, size_t c, const char *f, va_list ap) __attribute__((alias("vsnprintf")));
int siprintf(char *b, const char *f, ...) __attribute__((alias("sprintf")));

int fputc(int c, FILE *fp) { (void)fp; board_putchar(c); return (unsigned char)c; }
int putc(int c, FILE *fp) { return fputc(c, fp); }
int putchar(int c) { board_putchar(c); return (unsigned char)c; }
int fputs(const char *s, FILE *fp) { (void)fp; while (*s) board_putchar((unsigned char)*s++); return 0; }
int puts(const char *s) { fputs(s, stdout); board_putchar('\n'); return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *fp) {
  (void)fp; const unsigned char *b = p;
  for (size_t i = 0; i < sz * n; i++) board_putchar(b[i]);
  return n;
}
int fflush(FILE *fp) { (void)fp; return 0; }
int setvbuf(FILE *fp, char *buf, int mode, size_t size) { (void)fp; (void)buf; (void)mode; (void)size; return 0; }

/* ---- exit without stdio's stream cleanup -------------------------------- */
#define TINY_ATEXIT_MAX 8
static void (*tiny_atexit_fn[TINY_ATEXIT_MAX])(void);
static int tiny_atexit_n;
int atexit(void (*fn)(void)) {
  if (tiny_atexit_n >= TINY_ATEXIT_MAX) return -1;
  tiny_atexit_fn[tiny_atexit_n++] = fn;
  return 0;
}
__attribute__((weak, noreturn)) void board_halt(int status) { (void)status; for (;;) { } }
void exit(int status) {
  while (tiny_atexit_n > 0) tiny_atexit_fn[--tiny_atexit_n]();
  board_halt(status);
}
void _exit(int status) { board_halt(status); }

/* ---- no file system ------------------------------------------------------
 * The runtime's File and IO classes reach these; on a part with no file
 * system every open fails with ENOSYS (an honest Errno::ENOSYS in Ruby),
 * and newlib's FILE machinery stays out of the link. The console streams
 * above are the only ones that exist. */
#include <errno.h>
#include <sys/types.h>
FILE *fopen(const char *p, const char *m) { (void)p; (void)m; errno = ENOSYS; return NULL; }
FILE *fdopen(int fd, const char *m) { (void)fd; (void)m; errno = ENOSYS; return NULL; }
FILE *freopen(const char *p, const char *m, FILE *fp) { (void)p; (void)m; (void)fp; errno = ENOSYS; return NULL; }
int fclose(FILE *fp) { (void)fp; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *fp) { (void)p; (void)sz; (void)n; (void)fp; return 0; }
int fgetc(FILE *fp) { (void)fp; return EOF; }
int getc(FILE *fp) { (void)fp; return EOF; }
int getc_unlocked(FILE *fp) { (void)fp; return EOF; }
int ungetc(int c, FILE *fp) { (void)c; (void)fp; return EOF; }
long ftell(FILE *fp) { (void)fp; errno = ESPIPE; return -1; }
int fseek(FILE *fp, long off, int wh) { (void)fp; (void)off; (void)wh; errno = ESPIPE; return -1; }
void rewind(FILE *fp) { (void)fp; }
void clearerr(FILE *fp) { (void)fp; }
int ferror(FILE *fp) { (void)fp; return 0; }
int feof(FILE *fp) { (void)fp; return 1; }
int fileno(FILE *fp) { return fp == stdin ? 0 : fp == stdout ? 1 : 2; }
int putc_unlocked(int c, FILE *fp) { return fputc(c, fp); }
void perror(const char *s) { if (s && *s) { fputs(s, stderr); fputs(": ", stderr); } fputs("error\n", stderr); }
ssize_t __getdelim(char **l, size_t *n, int d, FILE *fp) { (void)l; (void)n; (void)d; (void)fp; return -1; }
ssize_t getdelim(char **l, size_t *n, int d, FILE *fp) { return __getdelim(l, n, d, fp); }
ssize_t __getline(char **l, size_t *n, FILE *fp) { return __getdelim(l, n, '\n', fp); }
ssize_t getline(char **l, size_t *n, FILE *fp) { return __getdelim(l, n, '\n', fp); }
int fseeko(FILE *fp, off_t off, int wh) { (void)fp; (void)off; (void)wh; errno = ESPIPE; return -1; }
off_t ftello(FILE *fp) { (void)fp; errno = ESPIPE; return -1; }
/* Time parsing (sp_time.c) is the runtime's only sscanf user; without scanf
   it reports no fields matched, and the Ruby call raises ArgumentError. */
int sscanf(const char *s, const char *f, ...) { (void)s; (void)f; return EOF; }
