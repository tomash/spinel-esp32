/* Espressif ESP32-C6-DevKitC-1: one WS2812 RGB LED on GPIO8, BOOT button on
 * GPIO9 (active low; also the strapping pin, so read it only after boot).
 * No battery. Console on the UART port (CP2102N). */
#include "board.h"
#include "board_common.h"

static const int buttons[] = { 9 };
int board_init(void) { return bc_buttons_init(buttons, 1); }
const char *board_name(void) { return "ESP32-C6-DevKitC-1"; }
const char *board_mdns_host(void) { return "spinel-esp32-c6"; }
int board_led_init(void) { return bc_ws2812_init(8); }
int board_led_set(int on) { return bc_ws2812_set(on); }
int board_battery_mv(void) { return -1; }
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 0; }
const board_display_t *board_display(void) { return 0; }
int board_display_power(int on) { (void)on; return -1; }
