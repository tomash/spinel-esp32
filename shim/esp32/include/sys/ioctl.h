/* shim/esp32/include/sys/ioctl.h -- newlib ships none. ESP-IDF's newlib
 * component declares int ioctl(int, int, ...) with no constants; the
 * winsize/TIOCOUTQ request codes below are the ESP-IDF esp_driver/uart
 * and termios component's (Linux-compatible) values, which is what the
 * ioctl() that newlib's syscall shim routes to expects.
 *
 * TIOCOUTQ is what sp_io.c's #elif defined(TIOCOUTQ) probe wants (Linux
 * path); TIOCGWINSZ/TIOCSWINSZ carry IO#winsize. struct winsize shape is
 * the universal one (4 unsigned shorts).
 */
#ifndef _SP_SHIM_SYS_IOCTL_H
#define _SP_SHIM_SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

/* Linux/ESP-IDF-compatible request codes (asm-generic ioctl encoding:
 * _IOR 0x54 'T' 4 bytes / plain) */
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCOUTQ    0x5411
#define TIOCNXCL    0x540D

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_SYS_IOCTL_H */
