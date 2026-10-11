/* shim/esp32/include/ucontext.h -- newlib (riscv32-esp-elf, xtensa-esp-elf)
 * ships no ucontext: the machine directories carry setjmp only.
 *
 * spinel's lib/sp_fiber_ctx.h falls back to ucontext when SP_FIBER_ASM is 0
 * (any target but x86_64/aarch64). This shim supplies a minimal cooperative
 * implementation for RV32IMAC (src/ucontext.S), modeled on libucontext's
 * riscv32 backend. xtensa (classic ESP32/S3) is a later milestone.
 *
 * Struct layout -- fixed, shared with src/ucontext.S:
 *   ucontext_t:
 *     +0   uc_link
 *     +4   uc_stack.ss_sp      +8  ss_flags     +12 ss_size
 *     +16  uc_mcontext.__regs[14]:
 *            [0] ra   [1] sp   [2] s0 (fp)   [3] s1
 *            [4..13] s2..s11
 *   makecontext additionally primes the NEW stack's top-16 frame:
 *     [top-16]=func  [top-8]=uc_link, and sets ra=__start_context,
 *     sp=top-16 -- so __start_context reads 0(sp)/8(sp) with sp 16-byte
 *     aligned, per the RV32 psABI.
 */
#ifndef SP_SHIM_ESP32_UCONTEXT_H
#define SP_SHIM_ESP32_UCONTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned long __regs[14];   /* ra, sp, s0, s1, s2..s11 */
} mcontext_t;

typedef struct ucontext_t {
    struct ucontext_t *uc_link;
    struct {
        void   *ss_sp;
        int     ss_flags;
        size_t  ss_size;
    } uc_stack;
    mcontext_t uc_mcontext;
} ucontext_t;

int getcontext(ucontext_t *ucp);
int setcontext(const ucontext_t *ucp);
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...);
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

#ifdef __cplusplus
}
#endif

#endif /* SP_SHIM_ESP32_UCONTEXT_H */
