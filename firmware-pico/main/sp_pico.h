/* firmware-pico/main/sp_pico.h -- shared between the Pico adapter, board and
 * network files. */
#pragma once

/* cyw43_arch_init once, on first use (it also starts lwIP and loads the
 * radio firmware; must run in a task, after the scheduler started).
 * 0 when the chip is up. */
int sp_pico_cyw43_up(void);
