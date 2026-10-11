/* shim/esp32/include/sys/un.h -- newlib ships none. Identical to
 * ESP-IDF's newlib platform_include/sys/un.h (Espressif's own): AF_UNIX
 * and sockaddr_un with the sun_family-as-short spelling. sp_net.c
 * computes a packed UNIX socket address length via offsetof(struct
 * sockaddr_un, sun_path) + strlen.
 */
#ifndef _SP_SHIM_SYS_UN_H
#define _SP_SHIM_SYS_UN_H

#ifdef __cplusplus
extern "C" {
#endif

#define AF_UNIX     1       /* local to host (pipes) */

struct sockaddr_un {
    short  sun_family;      /* AF_UNIX */
    char   sun_path[108];   /* path name */
};

#ifdef __cplusplus
}
#endif

#endif /* _SP_SHIM_SYS_UN_H */
