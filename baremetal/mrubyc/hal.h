/* Bare-metal HAL for measuring mruby/c on rv32imac (comparison only). */
#ifndef MRBC_SRC_HAL_H_
#define MRBC_SRC_HAL_H_
#define MRBC_TICK_UNIT 1
#define MRBC_TIMESLICE_TICK_COUNT 10
void mrbc_tick(void);
#define mrbc_hal_init()        ((void)0)
#define mrbc_hal_enable_irq()  ((void)0)
#define mrbc_hal_disable_irq() ((void)0)
#define mrbc_hal_idle_cpu()    (mrbc_tick())
int mrbc_hal_write(int fd, const void *buf, int nbytes);
int mrbc_hal_flush(int fd);
void mrbc_hal_abort(const char *s);
#endif
