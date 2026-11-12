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

/* The board LED in a colour, 0-255 per channel: boards with an addressable
 * RGB LED (WS2812/SK6812). -1 on boards whose LED has one colour. */
int board_led_rgb(int r, int g, int b);

/* Battery voltage in millivolts (ADC divider or PMIC); -1 when the board has
 * no battery measurement. */
int board_battery_mv(void);

/* User buttons, indexed from 0. board_button_pressed: 1 pressed, 0 released,
 * -1 no such button. */
int board_button_count(void);
int board_button_pressed(int idx);

/* Inertial sensor (M5StickC MPU6886, M5StickS3 BMI270), +-8 g / +-2000 dps.
 * board_imu_init: 0 ready, negative when the board has none or it did not
 * answer. board_imu_update reads one sample; the accessors return it per
 * axis (0 x, 1 y, 2 z) in milli-g and milli-degrees per second. */
int board_imu_init(void);
const char *board_imu_name(void);
int board_imu_update(void);
int board_imu_accel_mg(int axis);
int board_imu_gyro_mdps(int axis);

/* Infrared, NEC protocol (boards/ir.c). board_ir_init: 0 ready, -1 no IR.
 * board_ir_send_nec sends a 16-bit address and an 8-bit command (boards with
 * an IR LED: M5StickC, M5StickS3). board_ir_poll waits up to ms for a frame
 * from the IR receiver (M5StickS3): 1 frame (then board_ir_addr/cmd),
 * 2 repeat code, 0 nothing, -1 not NEC. A board describes its IR in
 * board_ir_pins(&tx, &rx) and board_ir_prepare(). */
int board_ir_init(void);
int board_ir_send_nec(int addr, int cmd);
int board_ir_poll(int ms);
int board_ir_addr(void);
int board_ir_cmd(void);
void board_ir_pins(int *tx, int *rx);
int board_ir_prepare(void);

/* Audio (boards/audio.c). board_speaker_tone plays a sine tone (M5StickS3:
 * ES8311 + amplifier); volume 0-100, held to 75 on battery power as M5Stack
 * advises. board_mic_level records ms of sound and answers its RMS level
 * (0-32767; M5StickS3 ES8311 mic, M5StickC PDM mic). Both -1 without the
 * hardware. */
int board_speaker_tone(int freq_hz, int ms, int volume_pct);
/* After a run of tones: drains the speaker path and switches the amplifier
 * off (tones keep it open in between, so a melody does not click). */
int board_speaker_stop(void);
int board_mic_level(int ms);

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
