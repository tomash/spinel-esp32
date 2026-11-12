/* Raspberry Pi Pico 2 W (RP2350): the board.h API on the Pico SDK.
 *
 *   LED     on the CYW43439 WiFi chip (WL_GPIO0), not an RP2350 GPIO: it
 *           needs cyw43_arch_init, which also loads the radio firmware, so
 *           it is brought up on first use (board_led_init/board_battery_mv).
 *   button  BOOTSEL: the flash chip-select line, read by letting it float
 *           for a moment with flash access (and interrupts) off -- the
 *           pico-examples picoboard/button method; code runs from RAM.
 *   VSYS    ADC3 on GP29 behind a 3:1 divider. GP29 doubles as the CYW43
 *           SPI clock, so the read holds the cyw43 lock.
 *
 * Everything else on board.h (IMU, IR, audio, display) answers -1 / 0.
 */
#include "board.h"
#include "sp_pico.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#define pico2w_cyw43_up sp_pico_cyw43_up   /* net_pico.c: shared with WiFi */

int board_init(void) { return 0; }
const char *board_name(void) { return "Pico 2 W (RP2350, RISC-V)"; }
const char *board_mdns_host(void) { return "spinel-mote-pico"; }

int board_led_init(void) { return pico2w_cyw43_up(); }
int board_led_set(int on)
{
    if (pico2w_cyw43_up() != 0) return -1;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
    return 0;
}
int board_led_rgb(int r, int g, int b) { (void)r; (void)g; (void)b; return -1; }

int board_battery_mv(void)
{
    if (pico2w_cyw43_up() != 0) return -1;
    static int adc_ready = 0;
    if (!adc_ready) { adc_init(); adc_ready = 1; }
    cyw43_thread_enter();
    /* wake the chip so it is not mid-transfer on the shared pin */
    cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
    adc_gpio_init(PICO_VSYS_PIN);
    adc_select_input(PICO_VSYS_PIN - ADC_BASE_PIN);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    cyw43_thread_exit();
    /* 12-bit, 3.3 V reference, 3:1 divider */
    return (int)(sum * 3u * 3300u / (8u * 4096u));
}

static bool __no_inline_not_in_flash_func(pico2w_bootsel)(void)
{
    const uint cs = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[cs].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) ;
    bool pressed = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
    hw_write_masked(&ioqspi_hw->io[cs].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
}
int board_button_count(void) { return 1; }
int board_button_pressed(int idx) { return idx == 0 ? (pico2w_bootsel() ? 1 : 0) : -1; }

int board_imu_init(void) { return -1; }
const char *board_imu_name(void) { return "none"; }
int board_imu_update(void) { return -1; }
int board_imu_accel_mg(int axis) { (void)axis; return 0; }
int board_imu_gyro_mdps(int axis) { (void)axis; return 0; }

int board_ir_init(void) { return -1; }
int board_ir_send_nec(int addr, int cmd) { (void)addr; (void)cmd; return -1; }
int board_ir_poll(int ms) { (void)ms; return -1; }
int board_ir_addr(void) { return -1; }
int board_ir_cmd(void) { return -1; }
void board_ir_pins(int *tx, int *rx) { if (tx) *tx = -1; if (rx) *rx = -1; }
int board_ir_prepare(void) { return -1; }

int board_speaker_tone(int freq_hz, int ms, int volume_pct) { (void)freq_hz; (void)ms; (void)volume_pct; return -1; }
int board_speaker_stop(void) { return -1; }
int board_mic_level(int ms) { (void)ms; return -1; }

int board_has_lcd(void) { return 0; }
const board_display_t *board_display(void) { return 0; }
int board_display_power(int on) { (void)on; return -1; }

/* The T-Display panel driver's API (firmware/main/tdisplay.c on ESP), which
 * apps such as wifi_status declare and call only when board_has_lcd() says
 * so: no panel here. */
int esp32_display_init(void) { return -1; }
int esp32_display_set_orientation(int o) { (void)o; return -1; }
int esp32_display_width(void) { return 0; }
int esp32_display_height(void) { return 0; }
int esp32_display_clear(int c) { (void)c; return -1; }
int esp32_display_rect(int x, int y, int w, int h, int c, int fill) { (void)x; (void)y; (void)w; (void)h; (void)c; (void)fill; return -1; }
int esp32_display_text(const char *s, int x, int y, int fg, int bg, int scale) { (void)s; (void)x; (void)y; (void)fg; (void)bg; (void)scale; return -1; }
