/* firmware/main/board.h -- the board support API the Ruby apps call.
 *
 * One implementation per board in firmware/main/boards/<board>.c, picked at
 * configure time by SPINEL_BOARD (firmware/spinel_target.cmake; each target
 * has a default board). Apps reach these through spinel's FFI DSL, e.g.
 *
 *     ffi_func :board_led_set, [:int], :int
 *
 * so the same app.rb runs on every board: what an LED, a button or the
 * battery is on a given board lives here, not in the app. Every function is
 * safe to call on a board without the feature: it answers 0 / -1 instead of
 * failing.
 */
#pragma once

/* Called once from app_main, before the Ruby program starts. Boards whose
 * power is held by a PMIC do that here (M5StickS3: disable the M5PM1
 * watchdog, or the PMIC switches the board off). 0 on success. */
int board_init(void);

/* Human-readable board name ("T-OI Plus (ESP32-C3)") and the mDNS host name
 * a networked app should claim ("spinel-esp32"; distinct per board so two
 * boards on one LAN do not fight over a name). */
const char *board_name(void);
const char *board_mdns_host(void);

/* The board's user LED: a GPIO LED, an addressable RGB LED (shown white), or
 * a PMIC-driven LED. board_led_set answers -1 when the board has none. */
int board_led_init(void);
int board_led_set(int on);

/* Battery voltage in millivolts (ADC divider or PMIC); -1 when the board has
 * no battery measurement. */
int board_battery_mv(void);

/* User buttons, indexed from 0. board_button_pressed: 1 pressed, 0 released,
 * -1 no such button. */
int board_button_count(void);
int board_button_pressed(int idx);

/* 1 when the board has a display driven by firmware/main (tdisplay.c). */
int board_has_lcd(void);

/* The display, for tdisplay.c (an ST7789-family SPI panel driver; ST7735S
 * speaks the same command set). Offsets are the visible window inside the
 * controller's RAM, per orientation; mirror flags are what makes text read
 * upright on the board as mounted. bl < 0: the backlight is not a GPIO, and
 * board_display_power() switches it (PMIC rails). NULL: no display. */
typedef struct {
    int host;                        /* SPI2_HOST / SPI3_HOST */
    int sclk, mosi, cs, dc, rst, bl;
    int width, height;               /* portrait, as mounted */
    int gap_x, gap_y;                /* portrait offsets */
    int gap_x_land, gap_y_land;      /* landscape offsets */
    int mirror_x, mirror_y;          /* portrait */
    int mirror_x_land, mirror_y_land;
    int invert, bgr;
    int swap_bytes;   /* send each RGB565 pixel byte-swapped: panels whose
                         byte order esp_lcd's ST7789 RAMCTRL cannot set
                         (ST7735S, where 0xB0 is a frame-rate register) */
    int pclk_hz;
} board_display_t;
const board_display_t *board_display(void);
int board_display_power(int on);
