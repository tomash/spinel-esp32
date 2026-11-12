/* mruby/c on bare metal, no scheduler (sample_no_scheduler.c shape). The
   bytecode lives in flash; a real image would come from mrbc on the host. */
#include <stdint.h>
#include "mrubyc.h"
static uint8_t memory_pool[8 * 1024];
extern const uint8_t app_mrb[];
volatile uint32_t sink;
int mrbc_hal_write(int fd, const void *buf, int n) { for (int i = 0; i < n; i++) sink = ((const uint8_t *)buf)[i]; return n; }
int mrbc_hal_flush(int fd) { return 0; }
void mrbc_hal_abort(const char *s) { for (;;) {} }
/* led(pin, on) -- a C method the Ruby program would call, like spinel's FFI */
static void c_led(mrbc_vm *vm, mrbc_value v[], int argc) {
  *(volatile uint32_t *)0x40010810 = mrbc_integer(v[2]) ? (1u << mrbc_integer(v[1])) : (1u << (mrbc_integer(v[1]) + 16));
}
int main(void) {
  mrbc_init_alloc(memory_pool, sizeof memory_pool);
  mrbc_init_global();
  mrbc_init_class();
  mrbc_define_method(0, mrbc_class_object, "led", c_led);
  mrbc_vm *vm = mrbc_vm_open(NULL);
  if (!vm) return 1;
  if (mrbc_load_mrb(vm, app_mrb) != 0) return 2;
  mrbc_vm_begin(vm);
  mrbc_vm_run(vm);
  mrbc_vm_end(vm);
  mrbc_vm_close(vm);
  return 0;
}
