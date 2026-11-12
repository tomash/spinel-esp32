/*
 * sp_pico_adapter.c -- startup adapter for the Pico SDK (the counterpart of
 * firmware/main/sp_app_adapter.c).
 *
 * The SDK's crt0 calls main() directly, and so would the generated TU's
 * main(argc, argv): CMakeLists.txt compiles app.c with -Dmain=spinel_app_main
 * so this main() runs first. It brings up stdio (USB CDC, plus UART0 for the
 * Debug Probe) and starts FreeRTOS with the Ruby program in its own task,
 * SP_PICO_STACK bytes of stack, as ESP-IDF's main task. lwIP's tcpip thread
 * and the CYW43 driver (pico_cyw43_arch_lwip_sys_freertos) run beside it.
 *
 * The shim mmap answers MAP_FAILED, so sp_main_stack_run runs the Ruby body
 * in place, on the task's stack.
 */
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#ifndef SP_PICO_STACK
#define SP_PICO_STACK (48 * 1024)   /* as CONFIG_ESP_MAIN_TASK_STACK_SIZE */
#endif
#define SP_PICO_PRIO  (tskIDLE_PRIORITY + 2)   /* below lwIP and CYW43 */

int spinel_app_main(int argc, char **argv);

/* Boot guard (bring-up safety net): a firmware that crashes before its USB
 * serial port is up cannot be reached by picotool, and needs a hand on the
 * BOOTSEL button. So main() arms the watchdog and leaves a marker in a
 * watchdog scratch register (it survives a watchdog reset, not a power
 * cycle); the Ruby task clears both once the system is up. If startup
 * crashes or hangs instead, the watchdog reboots the chip, the next boot
 * finds the marker and enters BOOTSEL, ready for `picotool load`.
 * Scratch 0 is free: the SDK's watchdog_reboot uses 4-7. */
#ifndef SP_PICO_BOOT_GUARD_MS
#define SP_PICO_BOOT_GUARD_MS 8000   /* > the 5 s USB connect wait */
#endif
#define SP_PICO_GUARD_MAGIC 0x5350475du  /* "SPG]" */

static void sp_pico_boot_guard_arm(void)
{
#if SP_PICO_BOOT_GUARD_MS > 0
    /* Only a watchdog *timeout* counts: a forced reboot (picotool, the
     * BOOTSEL loader, the fault handler) can land before the Ruby task
     * cleared the marker without anything having gone wrong. */
    if (watchdog_hw->scratch[0] == SP_PICO_GUARD_MAGIC &&
        (watchdog_hw->reason & WATCHDOG_REASON_TIMER_BITS)) {
        watchdog_hw->scratch[0] = 0;
        reset_usb_boot(0, 0);   /* previous boot never came up */
    }
    watchdog_hw->scratch[0] = SP_PICO_GUARD_MAGIC;
    watchdog_enable(SP_PICO_BOOT_GUARD_MS, true);
#endif
}

static void sp_pico_boot_guard_clear(void)
{
#if SP_PICO_BOOT_GUARD_MS > 0
    watchdog_disable();
    watchdog_hw->scratch[0] = 0;
#endif
}

/* CPU exceptions (misaligned access, bus fault, illegal instruction): the
 * FreeRTOS port's default handler spins (`j .`), which looks like a silent
 * hang -- the core stops, USB half-answers, the network dies. This one
 * records the cause in watchdog scratch 1-3 (they survive anything but a
 * power cycle) and enters BOOTSEL; the next image to boot prints it. Printing from here is not reliable:
 * USB serial needs interrupts. */
#define SP_PICO_FAULT_MAGIC 0xFA000000u
/* The boot ROM's BOOTSEL entry reuses watchdog scratch 2-3 and SRAM, so
 * the faulting pc rides in scratch 0 (the boot guard's, free at that
 * point); main() takes it before re-arming the guard. */
static uint32_t sp_pico_fault_pc;

void freertos_risc_v_application_exception_handler(void)
{
    uint32_t mcause, mepc, mtval;
    __asm volatile ("csrr %0, mcause" : "=r"(mcause));
    __asm volatile ("csrr %0, mepc" : "=r"(mepc));
    __asm volatile ("csrr %0, mtval" : "=r"(mtval));
    watchdog_hw->scratch[1] = SP_PICO_FAULT_MAGIC | (mcause & 0xffffu);
    (void)mtval;
    watchdog_hw->scratch[0] = mepc;       /* also: not a boot-guard failure */
    /* Into BOOTSEL, ready for a fixed image (the next one loaded prints the
     * report): a reboot straight back into the app -- watchdog_reboot or
     * the ROM's normal reboot -- hangs that boot before the scheduler starts
     * (open issue, FreeRTOS SMP on the Hazard3 cores; a power-up or the
     * BOOTSEL loader's boot is fine). */
    reset_usb_boot(0, 0);
    for (;;) ;
}

static void sp_pico_report_fault(void)
{
    uint32_t f = watchdog_hw->scratch[1];
    if ((f & 0xffff0000u) != SP_PICO_FAULT_MAGIC) return;
    static const char *const names[] = {
        "instruction misaligned", "instruction fault", "illegal instruction",
        "breakpoint", "load misaligned", "load fault", "store misaligned",
        "store fault" };
    uint32_t cause = f & 0xffffu;
    printf("spinel: previous run stopped on a CPU exception: %s (mcause %lu)",
           cause < 8 ? names[cause] : "other", (unsigned long)cause);
    printf(" at pc 0x%08lx (look it up: riscv32-pico-elf-addr2line -fe "
           "firmware-pico/build/spinel_mote_app.elf 0x%08lx)\n",
           (unsigned long)sp_pico_fault_pc, (unsigned long)sp_pico_fault_pc);
    watchdog_hw->scratch[1] = 0;
}

static void sp_pico_task(void *arg)
{
    (void)arg;
    sp_pico_boot_guard_clear();
    sp_pico_report_fault();
    int brc = board_init();
    printf("spinel: board: %s (init %d)\n", board_name(), brc);
    printf("spinel: calling generated main() in a FreeRTOS task, %u-byte stack\n",
           (unsigned)SP_PICO_STACK);
    /* argc=0/argv=NULL: no ARGV on target; spinel's TU only stashes them. */
    int rc = spinel_app_main(0, NULL);
    printf("spinel: main() returned %d\n", rc);
    /* Keep the task (and USB serial, and the 1200-baud BOOTSEL reset) alive. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

int main(void)
{
    if ((watchdog_hw->scratch[1] & 0xffff0000u) == SP_PICO_FAULT_MAGIC)
        sp_pico_fault_pc = watchdog_hw->scratch[0];
    sp_pico_boot_guard_arm();
    stdio_init_all();
    xTaskCreate(sp_pico_task, "spinel", SP_PICO_STACK / sizeof(StackType_t),
                NULL, SP_PICO_PRIO, NULL);
    vTaskStartScheduler();
    for (;;) ;   /* not reached */
}

/* Kernel#sleep and the scheduler's waits: block the task, not the core (the
 * runtime stub's busy-wait nanosleep is weak; this one wins). */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) { errno = EINVAL; return -1; }
    uint64_t us = (uint64_t)req->tv_sec * 1000000u + (uint64_t)req->tv_nsec / 1000u;
    TickType_t t = (TickType_t)((us + (1000000u / configTICK_RATE_HZ) - 1) / (1000000u / configTICK_RATE_HZ));
    if (t == 0 && us > 0) t = 1;
    if (t) vTaskDelay(t);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

/* exit()/abort() from the Ruby program (an uncaught exception ends in
 * exit(1)): the SDK's _exit stops the core at a breakpoint, which takes USB
 * and the network down with it. Stop only the calling task instead, so the
 * message stays readable and picotool can still reboot the board. */
void _exit(int status)
{
    printf("spinel: exit(%d)\n", status);
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        for (;;) vTaskSuspend(NULL);
    for (;;) tight_loop_contents();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    printf("spinel: FreeRTOS stack overflow in task %s\n", name);
    for (;;) ;
}

void vApplicationMallocFailedHook(void)
{
    printf("spinel: FreeRTOS malloc failed\n");
}
