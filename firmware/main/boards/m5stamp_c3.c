/* M5Stack M5Stamp C3 (ESP32-C3): one WS2812 RGB LED on GPIO2, user button
 * GPIO3 (active low). No battery. */
#include "board.h"
#include "board_common.h"

static const int buttons[] = { 3 };
int board_init(void) { return bc_buttons_init(buttons, 1); }
const char *board_name(void) { return "M5Stamp C3 (ESP32-C3)"; }
const char *board_mdns_host(void) { return "spinel-m5stamp-c3"; }
int board_led_init(void) { return bc_ws2812_init(2); }
int board_led_set(int on) { return bc_ws2812_set(on); }
int board_battery_mv(void) { return -1; }
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 0; }
const board_display_t *board_display(void) { return 0; }
int board_display_power(int on) { (void)on; return -1; }
int board_led_rgb(int r, int g, int b) { return bc_ws2812_rgb(r, g, b); }
