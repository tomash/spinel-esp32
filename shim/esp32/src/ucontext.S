/* shim/esp32/src/ucontext.S -- RV32IMAC coroutine context switch.
 *
 * newlib (riscv32-esp-elf) has no ucontext implementation; spinel's
 * lib/sp_fiber_ctx.h needs one when SP_FIBER_ASM is 0 (its fallback path,
 * used on every target that is not x86_64/aarch64). Modeled on libucontext's
 * riscv32 backend: save/restore the integer callee-saved set (ra, sp, s0-fp,
 * s1..s11) -- exactly the registers the RV32 ABI makes the callee preserve.
 * A cooperative switch needs nothing else: a0-a7/t0-t6 and the fp registers
 * are caller-saved and are already spilled by compiled prologues where live.
 * (The ESP32-C3 FPU is unused under ESP-IDF's default soft-float ABI; if
 * that ever changes, f8..f11 handling goes here.)
 *
 * Struct offsets (see include/ucontext.h; all in bytes from ucp):
 *   +0   uc_link
 *   +4   uc_stack.ss_sp    +8 ss_flags   +12 ss_size
 *   +16  uc_mcontext.__regs[0..13]:
 *          [0] ra  [1] sp  [2] s0  [3] s1  [4..13] s2..s11
 */

	.text
	.align  2

/* int getcontext(ucontext_t *ucp) -- a0 */
	.globl  getcontext
	.type   getcontext, @function
getcontext:
	sw      ra, 16 + 4*0(a0)
	sw      sp, 16 + 4*1(a0)
	sw      s0, 16 + 4*2(a0)
	sw      s1, 16 + 4*3(a0)
	sw      s2, 16 + 4*4(a0)
	sw      s3, 16 + 4*5(a0)
	sw      s4, 16 + 4*6(a0)
	sw      s5, 16 + 4*7(a0)
	sw      s6, 16 + 4*8(a0)
	sw      s7, 16 + 4*9(a0)
	sw      s8, 16 + 4*10(a0)
	sw      s9, 16 + 4*11(a0)
	sw      s10,16 + 4*12(a0)
	sw      s11,16 + 4*13(a0)
	li      a0, 0
	ret
	.size   getcontext, . - getcontext

/* int setcontext(const ucontext_t *ucp) -- a0 */
	.globl  setcontext
	.type   setcontext, @function
setcontext:
	lw      s0, 16 + 4*2(a0)      /* everything but sp and ra first */
	lw      s1, 16 + 4*3(a0)
	lw      s2, 16 + 4*4(a0)
	lw      s3, 16 + 4*5(a0)
	lw      s4, 16 + 4*6(a0)
	lw      s5, 16 + 4*7(a0)
	lw      s6, 16 + 4*8(a0)
	lw      s7, 16 + 4*9(a0)
	lw      s8, 16 + 4*10(a0)
	lw      s9, 16 + 4*11(a0)
	lw      s10,16 + 4*12(a0)
	lw      s11,16 + 4*13(a0)
	lw      ra, 16 + 4*0(a0)      /* then ra, */
	lw      sp, 16 + 4*1(a0)      /* then the stack last */
	li      a0, 0
	ret
	.size   setcontext, . - setcontext

/* int swapcontext(ucontext_t *oucp, const ucontext_t *ucp) -- a0=save, a1=load */
	.globl  swapcontext
	.type   swapcontext, @function
swapcontext:
	sw      ra, 16 + 4*0(a0)
	sw      sp, 16 + 4*1(a0)
	sw      s0, 16 + 4*2(a0)
	sw      s1, 16 + 4*3(a0)
	sw      s2, 16 + 4*4(a0)
	sw      s3, 16 + 4*5(a0)
	sw      s4, 16 + 4*6(a0)
	sw      s5, 16 + 4*7(a0)
	sw      s6, 16 + 4*8(a0)
	sw      s7, 16 + 4*9(a0)
	sw      s8, 16 + 4*10(a0)
	sw      s9, 16 + 4*11(a0)
	sw      s10,16 + 4*12(a0)
	sw      s11,16 + 4*13(a0)

	mv      t0, a1                /* ucp is lost once sp moves; keep it in t0 */
	lw      s0, 16 + 4*2(t0)
	lw      s1, 16 + 4*3(t0)
	lw      s2, 16 + 4*4(t0)
	lw      s3, 16 + 4*5(t0)
	lw      s4, 16 + 4*6(t0)
	lw      s5, 16 + 4*7(t0)
	lw      s6, 16 + 4*8(t0)
	lw      s7, 16 + 4*9(t0)
	lw      s8, 16 + 4*10(t0)
	lw      s9, 16 + 4*11(t0)
	lw      s10,16 + 4*12(t0)
	lw      s11,16 + 4*13(t0)
	lw      ra, 16 + 4*0(t0)
	lw      sp, 16 + 4*1(t0)
	li      a0, 0
	ret
	.size   swapcontext, . - swapcontext

/* void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
 * spinel calls it with argc==0 (sp_fiber_ctx.h). The varargs form with argc>0
 * is not implemented; it is a link error if ever reached.
 */
	.globl  makecontext
	.type   makecontext, @function
makecontext:
	/* a0=ucp, a1=func, a2=argc (unused: spinel always passes 0) */
	/* top = (ss_sp + ss_size) & ~15 */
	lw      t0, 4(a0)             /* ss_sp */
	lw      t1, 12(a0)            /* ss_size */
	add     t0, t0, t1
	li      t2, ~15
	and     t0, t0, t2            /* t0 = 16-byte aligned stack top */
	/* the __start_context frame, at the very top of the new stack:
	   [top-16] = func   [top-8] = uc_link   -> sp enters 16-byte aligned */
	sw      a1, -16(t0)
	lw      t3, 0(a0)             /* uc_link */
	sw      t3, -8(t0)
	/* regs: ra = __start_context, sp = top-16, everything else zero */
	la      t1, __start_context
	sw      t1, 16 + 4*0(a0)
	addi    t2, t0, -16
	sw      t2, 16 + 4*1(a0)
	sw      zero, 16 + 4*2(a0)
	sw      zero, 16 + 4*3(a0)
	sw      zero, 16 + 4*4(a0)
	sw      zero, 16 + 4*5(a0)
	sw      zero, 16 + 4*6(a0)
	sw      zero, 16 + 4*7(a0)
	sw      zero, 16 + 4*8(a0)
	sw      zero, 16 + 4*9(a0)
	sw      zero, 16 + 4*10(a0)
	sw      zero, 16 + 4*11(a0)
	sw      zero, 16 + 4*12(a0)
	sw      zero, 16 + 4*13(a0)
	ret
	.size   makecontext, . - makecontext

/* __start_context: the first thing a makecontext'd coroutine runs.
 * Enters with sp at the top-16 frame: 0(sp)=func, 8(sp)=uc_link. */
	.globl  __start_context
	.type   __start_context, @function
__start_context:
	lw      a0, 0(sp)             /* func */
	jalr    a0                    /* call it; control returns here when done */
	lw      a0, 8(sp)             /* uc_link */
	beqz    a0, 1f
	jalr    a0                    /* chain into the link context's ra */
1:	/* no link: spinel's fiber trampoline never lets this happen (it swaps
	   away instead of returning); park forever rather than run off the end */
	wfi
	j       1b
	.size   __start_context, . - __start_context
