/* LilyGO TTGO T-Display (classic ESP32): no user LED, so "the LED" is the
 * ST7789 backlight (GPIO4); buttons GPIO0 / GPIO35; battery divider (x2) on
 * GPIO34 = ADC1_CH6. The panel itself is driven by tdisplay.c. */
#include "board.h"
#include "board_common.h"
#include "driver/spi_master.h"

static const int buttons[] = { 0, 35 };
int board_init(void) { return bc_buttons_init(buttons, 2); }
const char *board_name(void) { return "TTGO T-Display"; }
const char *board_mdns_host(void) { return "spinel-mote-tdisplay"; }
int board_led_init(void) { return bc_gpio_led_init(4, 0); }
int board_led_set(int on) { return bc_gpio_led_set(on); }
int board_battery_mv(void) { bc_adc_battery_init(ADC_CHANNEL_6, 2); return bc_adc_battery_mv(); }
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 1; }

/* ST7789V 135x240 at 52/40 in its RAM; landscape is MV|MX with the offsets
   swapped and the short-edge one grown by the missing column (40/53). */
static const board_display_t s_disp = {
    .host = SPI2_HOST, .sclk = 18, .mosi = 19, .cs = 5, .dc = 16, .rst = 23, .bl = 4,
    .width = 135, .height = 240, .gap_x = 52, .gap_y = 40, .gap_x_land = 40, .gap_y_land = 53,
    .mirror_x = 0, .mirror_y = 0, .mirror_x_land = 1, .mirror_y_land = 0,
    .invert = 1, .bgr = 0, .pclk_hz = 40 * 1000 * 1000,
};
const board_display_t *board_display(void) { return &s_disp; }
int board_display_power(int on) { (void)on; return 0; }
