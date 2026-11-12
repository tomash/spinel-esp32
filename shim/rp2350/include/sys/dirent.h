/* shim/rp2350/include/sys/dirent.h -- the Pico newlib's sys/dirent.h is an
 * #error. ESP-IDF supplies a VFS-backed one; the Pico has no file system, so
 * this declares the types and src/sp_rp2350_stub.c fails every call (ENOSYS),
 * which the runtime's Dir methods raise as Errno. */
#pragma once
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_REG     1
#define DT_DIR     2

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[256];
};

typedef struct { int dd_fd; } DIR;

DIR *opendir(const char *name);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
long telldir(DIR *d);
void seekdir(DIR *d, long loc);
int dirfd(DIR *d);
