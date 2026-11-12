/* sp_pico_stack.S -- call a function on another stack.
 *
 *   void sp_pico_call_on_stack(void (*fn)(void), void *stack_top);
 *
 * The SDK gives core 0 a 4 KB stack in SCRATCH_Y; the Ruby program needs
 * tens of KB (ESP-IDF's main task gets 48 KB). sp_pico_adapter.c runs the
 * generated main() on a static stack in main RAM through this. */
#if defined(__riscv)
    .section .text.sp_pico_call_on_stack, "ax"
    .global sp_pico_call_on_stack
    .type sp_pico_call_on_stack, %function
sp_pico_call_on_stack:
    mv   t0, sp
    andi a1, a1, -16          /* ABI: 16-byte aligned */
    addi sp, a1, -16
    sw   ra, 12(sp)
    sw   t0, 8(sp)
    jalr a0
    lw   ra, 12(sp)
    lw   t0, 8(sp)
    mv   sp, t0
    ret
    .size sp_pico_call_on_stack, . - sp_pico_call_on_stack
#elif defined(__arm__)
    .syntax unified
    .thumb
    .section .text.sp_pico_call_on_stack, "ax"
    .global sp_pico_call_on_stack
    .type sp_pico_call_on_stack, %function
    .thumb_func
sp_pico_call_on_stack:
    push {r4, lr}
    mov  r4, sp
    bic  r1, r1, #7           /* AAPCS: 8-byte aligned */
    mov  sp, r1
    blx  r0
    mov  sp, r4
    pop  {r4, pc}
    .size sp_pico_call_on_stack, . - sp_pico_call_on_stack
#else
#error "sp_pico_call_on_stack: unsupported architecture"
#endif
