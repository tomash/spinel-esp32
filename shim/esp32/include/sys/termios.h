/* shim/esp32/include/sys/termios.h -- the termios surface lib/sp_io.c's
 * io/console support (IO#raw, IO#noecho) compiles against.
 *
 * The bare cross toolchain's <termios.h> includes <sys/termios.h>, which only
 * ESP-IDF supplies (components/newlib/platform_include, not on the runtime
 * build's include path). Same layout and values as IDF's header, so a program
 * that does use IO#raw links against IDF's VFS tcgetattr/tcsetattr
 * (CONFIG_VFS_SUPPORT_TERMIOS). IDF has no cfmakeraw; the static inline below
 * is the POSIX definition. A program that never calls IO#raw drops all of it
 * with --gc-sections. */
#ifndef SP_ESP32_SHIM_SYS_TERMIOS_H
#define SP_ESP32_SHIM_SYS_TERMIOS_H
#include <stdint.h>

#define VMIN    5
#define VTIME   10
#define NCCS    (VTIME + 1)

#define BRKINT  (1u << 0)
#define ICRNL   (1u << 1)
#define IGNBRK  (1u << 2)
#define IGNCR   (1u << 3)
#define INLCR   (1u << 5)
#define ISTRIP  (1u << 7)
#define IXON    (1u << 11)
#define PARMRK  (1u << 12)

#define OPOST   (1u << 0)

#define CSIZE   (3u << 0)
#define CS8     (3u << 0)
#define PARENB  (1u << 4)

#define ECHO    (1u << 0)
#define ECHOE   (1u << 1)
#define ECHOK   (1u << 2)
#define ECHONL  (1u << 3)
#define ICANON  (1u << 4)
#define IEXTEN  (1u << 5)
#define ISIG    (1u << 6)

#define TCSANOW 0

typedef uint8_t  cc_t;
typedef uint32_t speed_t;
typedef uint16_t tcflag_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

int tcgetattr(int fd, struct termios *p);
int tcsetattr(int fd, int optional_actions, const struct termios *p);

static inline void cfmakeraw(struct termios *t) {
    t->c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= (tcflag_t)~OPOST;
    t->c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= (tcflag_t)~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

#endif
