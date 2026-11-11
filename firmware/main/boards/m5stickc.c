/* M5Stack M5StickC (ESP32-PICO-D4): red LED GPIO10 active-low; button A
 * GPIO37, B GPIO39 (input-only pins with board pull-ups). Power, battery and
 * the LCD rails belong to an AXP192 PMIC at 0x34 on I2C SDA21/SCL22; the
 * battery voltage is a 12-bit reading in 0x78:0x79, 1.1 mV per LSB. */
#include "board.h"
#include "board_common.h"

static const int buttons[] = { 37, 39 };
static int s_pmic_ok;
int board_init(void) {
    bc_buttons_init(buttons, 2);
    s_pmic_ok = bc_i2c_init(21, 22, 0x34, 400000) == 0;
    return 0;
}
const char *board_name(void) { return "M5StickC (ESP32-PICO-D4)"; }
const char *board_mdns_host(void) { return "spinel-m5stickc"; }
int board_led_init(void) { return bc_gpio_led_init(10, 1); }
int board_led_set(int on) { return bc_gpio_led_set(on); }
int board_battery_mv(void) {
    uint8_t b[2];
    if (!s_pmic_ok || bc_i2c_read(0x78, b, 2) != 0) return -1;
    return (int)((((uint32_t)b[0] << 4) | (b[1] & 0x0F)) * 11 / 10);
}
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 0; }   /* ST7735S 80x160: not driven yet */
