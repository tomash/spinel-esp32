/* LilyGO T-OI Plus (ESP32-C3): LED GPIO3 active-high; 18650 cell behind a
 * 100k/100k divider on GPIO2 = ADC1_CH2 (not CH0: GPIO0 is a floating Grove
 * pin). The mDNS name is the bare spinel-mote. */
#include "board.h"
#include "board_common.h"

int board_init(void) { return 0; }
const char *board_name(void) { return "T-OI Plus (ESP32-C3)"; }
const char *board_mdns_host(void) { return "spinel-mote"; }
int board_led_init(void) { return bc_gpio_led_init(3, 0); }
int board_led_set(int on) { return bc_gpio_led_set(on); }
int board_battery_mv(void) { bc_adc_battery_init(ADC_CHANNEL_2, 2); return bc_adc_battery_mv(); }
int board_button_count(void) { return 0; }
int board_button_pressed(int idx) { (void)idx; return -1; }
int board_has_lcd(void) { return 0; }
const board_display_t *board_display(void) { return 0; }
int board_display_power(int on) { (void)on; return -1; }
