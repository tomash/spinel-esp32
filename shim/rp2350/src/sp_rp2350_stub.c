/*
 * sp_rp2350_stub.c -- what the Pico SDK's newlib port lacks and ESP-IDF
 * supplies, for libspinel_rt on the RP2350 (built after sp_esp32_stub.c,
 * which still covers mmap, signals, processes and sockets).
 *
 *   - clock_gettime / nanosleep on the SDK's 64-bit microsecond timer
 *     (time_us_64, sleep_us: hardware_timer / pico_time). CLOCK_REALTIME is
 *     time since boot plus whatever settimeofday set; there is no RTC.
 *   - directory calls: no file system, every call fails with ENOSYS.
 *   - flockfile/funlockfile: this newlib is built without stdio locks; the
 *     Ruby program runs on one core, so there is nothing to lock.
 *
 * Built without the SDK's headers (the archive is SDK-independent), so the
 * two SDK functions are declared here; the firmware link resolves them.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <sys/dirent.h>

uint64_t time_us_64(void);
void sleep_us(uint64_t us);

static int64_t sp_rp2350_realtime_offset_us;

int clock_gettime(clockid_t clk, struct timespec *ts) {
    if (!ts) { errno = EINVAL; return -1; }
    int64_t us = (int64_t)time_us_64();
    if (clk == CLOCK_REALTIME) us += sp_rp2350_realtime_offset_us;
    ts->tv_sec = (time_t)(us / 1000000);
    ts->tv_nsec = (long)(us % 1000000) * 1000;
    return 0;
}

int clock_settime(clockid_t clk, const struct timespec *ts) {
    if (clk != CLOCK_REALTIME || !ts) { errno = EINVAL; return -1; }
    sp_rp2350_realtime_offset_us = (int64_t)ts->tv_sec * 1000000 + ts->tv_nsec / 1000
                                   - (int64_t)time_us_64();
    return 0;
}

int clock_getres(clockid_t clk, struct timespec *res) {
    (void)clk;
    if (res) { res->tv_sec = 0; res->tv_nsec = 1000; }
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) { errno = EINVAL; return -1; }
    sleep_us((uint64_t)req->tv_sec * 1000000u + (uint64_t)req->tv_nsec / 1000u);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

DIR *opendir(const char *name) { (void)name; errno = ENOSYS; return NULL; }
DIR *fdopendir(int fd) { (void)fd; errno = ENOSYS; return NULL; }
struct dirent *readdir(DIR *d) { (void)d; errno = EBADF; return NULL; }
int closedir(DIR *d) { (void)d; errno = EBADF; return -1; }
void rewinddir(DIR *d) { (void)d; }
long telldir(DIR *d) { (void)d; errno = EBADF; return -1; }
void seekdir(DIR *d, long loc) { (void)d; (void)loc; }
int dirfd(DIR *d) { (void)d; errno = EBADF; return -1; }

void flockfile(FILE *f) { (void)f; }
void funlockfile(FILE *f) { (void)f; }
int ftrylockfile(FILE *f) { (void)f; return 0; }
