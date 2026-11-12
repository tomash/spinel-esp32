/*
 * sp_pico_adapter.c -- startup adapter for the Pico SDK (the counterpart of
 * firmware/main/sp_app_adapter.c).
 *
 * The SDK's crt0 calls main() directly, and so would the generated TU's
 * main(argc, argv): CMakeLists.txt compiles app.c with -Dmain=spinel_app_main
 * so this main() runs first. It brings up stdio (USB CDC, plus UART0 for the
 * Debug Probe), the board, then calls the program on SP_PICO_STACK bytes of
 * static stack: the SDK's own core 0 stack is 4 KB in SCRATCH_Y.
 *
 * The shim mmap answers MAP_FAILED, so sp_main_stack_run runs the Ruby body
 * in place, on this stack.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "board.h"

#ifndef SP_PICO_STACK
#define SP_PICO_STACK (48 * 1024)   /* as CONFIG_ESP_MAIN_TASK_STACK_SIZE */
#endif

int spinel_app_main(int argc, char **argv);
void sp_pico_call_on_stack(void (*fn)(void), void *stack_top);

static uint8_t sp_pico_stack[SP_PICO_STACK] __attribute__((aligned(16)));
static int sp_pico_rc;

static void sp_pico_run(void)
{
    /* argc=0/argv=NULL: no ARGV on target; spinel's TU only stashes them. */
    sp_pico_rc = spinel_app_main(0, NULL);
}

int main(void)
{
    stdio_init_all();
    int brc = board_init();
    printf("spinel: board: %s (init %d)\n", board_name(), brc);
    printf("spinel: calling generated main() on a %u-byte stack\n", (unsigned)SP_PICO_STACK);
    sp_pico_call_on_stack(sp_pico_run, sp_pico_stack + sizeof sp_pico_stack);
    printf("spinel: main() returned %d\n", sp_pico_rc);
    /* Keep USB serial alive (and the 1200-baud reset into BOOTSEL working). */
    for (;;) sleep_ms(1000);
}
