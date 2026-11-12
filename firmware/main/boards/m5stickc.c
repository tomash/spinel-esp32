/* M5Stack M5StickC (ESP32-PICO-D4): red LED GPIO10 active-low; button A
 * GPIO37, B GPIO39 (input-only pins with board pull-ups). Power, battery and
 * the LCD rails belong to an AXP192 PMIC at 0x34 on I2C SDA21/SCL22; the
 * battery voltage is a 12-bit reading in 0x78:0x79, 1.1 mV per LSB. */
#include "board.h"
#include "board_common.h"
#include "driver/spi_master.h"

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
int board_has_lcd(void) { return 1; }

static const board_display_t s_disp = {
    .host = SPI2_HOST, .sclk = 13, .mosi = 15, .cs = 5, .dc = 23, .rst = 18, .bl = -1,
    .width = 80, .height = 160, .gap_x = 26, .gap_y = 1, .gap_x_land = 1, .gap_y_land = 26,
    .mirror_x = 1, .mirror_y = 1, .mirror_x_land = 1, .mirror_y_land = 0,
    .invert = 1, .bgr = 1, .swap_bytes = 1, .pclk_hz = 27 * 1000 * 1000,
};
const board_display_t *board_display(void) { return &s_disp; }
int board_display_power(int on) {
    uint8_t en;
    if (!s_pmic_ok || bc_i2c_read(0x12, &en, 1) != 0) return -1;
    bc_i2c_write8(0x28, 0xCC);                  /* LDO2 = LDO3 = 3.0 V */
    en = on ? (en | 0x0C) : (en & ~0x0C);       /* bit2 LDO2, bit3 LDO3 */
    return bc_i2c_write8(0x12, en);
}
#include "imu.h"
int board_imu_init(void) { return s_pmic_ok ? imu_init(IMU_MPU6886, 0x68) : -1; }
/* IR: an IR LED on G9, no receiver */
void board_ir_pins(int *tx, int *rx) { *tx = 9; *rx = -1; }
/* Mic: SPM1423 PDM, clock G0, data G34 */
#include "audio.h"
int board_mic_level(int ms) { return pdm_mic_level(0, 34, ms); }
