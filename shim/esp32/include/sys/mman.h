/* shim/esp32/include/sys/mman.h -- newlib has no <sys/mman.h>.
 *
 * spinel's runtime uses mmap for exactly two things: reserving fiber stacks
 * and the main stack (guarded, MAP_NORESERVE), and one optional
 * large-allocation path in sp_iobuffer. Both callers treat MAP_FAILED as a
 * documented, handled outcome:
 *
 *   - sp_main_stack_run (lib/sp_fiber.c): "If one cannot be had -- any size,
 *     down to the floor -- run it where we are: a program must not fail to
 *     start because it could not reserve a big stack." The body then runs on
 *     the FreeRTOS task stack. No upstream patch needed.
 *   - sp_Fiber_new: raises FiberError ("failed to allocate fiber stack") --
 *     honest behavior on a chip without mmap. Fiber support on ESP32 arrives
 *     with a static-pool override in this shim, later milestone.
 *   - sp_iobuffer's mmap fast path: falls back to malloc (it checks).
 *
 * So: declare the interface, define the constants the runtime references,
 * and let the definitions in sp_esp32_stub.c answer MAP_FAILED. The runtime
 * compiles unchanged; only features that truly need mmap fail, at run time.
 */
#ifndef SP_SHIM_ESP32_SYS_MMAN_H
#define SP_SHIM_ESP32_SYS_MMAN_H

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_PRIVATE  0x02
#define MAP_ANONYMOUS 0x20
#define MAP_SHARED   0x01
#define MAP_FAILED   ((void *)-1)
/* The runtime #defines these itself when absent (lib/sp_fiber.c:21-25); the
   shim's values are what the runtime would have picked. */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef MAP_STACK
#define MAP_STACK 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);

#ifdef __cplusplus
}
#endif

#endif /* SP_SHIM_ESP32_SYS_MMAN_H */
