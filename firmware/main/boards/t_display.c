/* LilyGO TTGO T-Display (classic ESP32): no user LED, so "the LED" is the
 * ST7789 backlight (GPIO4); buttons GPIO0 / GPIO35; battery divider (x2) on
 * GPIO34 = ADC1_CH6. The panel itself is driven by tdisplay.c. */
#include "board.h"
#include "board_common.h"

static const int buttons[] = { 0, 35 };
int board_init(void) { return bc_buttons_init(buttons, 2); }
const char *board_name(void) { return "TTGO T-Display"; }
const char *board_mdns_host(void) { return "spinel-esp32-tdisplay"; }
int board_led_init(void) { return bc_gpio_led_init(4, 0); }
int board_led_set(int on) { return bc_gpio_led_set(on); }
int board_battery_mv(void) { bc_adc_battery_init(ADC_CHANNEL_6, 2); return bc_adc_battery_mv(); }
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 1; }
