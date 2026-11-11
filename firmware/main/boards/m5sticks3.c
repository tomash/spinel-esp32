/* M5Stack M5StickS3 (ESP32-S3-PICO-1, 8 MB flash / 8 MB PSRAM).
 *
 * Power is held by an M5PM1 PMIC at 0x6E on I2C SDA47/SCL48. Its watchdog
 * and I2C idle sleep must be disabled at boot (what M5Unified's
 * M5PM1_Class::begin does), or the PMIC switches the board off -- which is
 * what a plain reset looked like before this board file existed. The PMIC
 * also owns the green LED (PWR_CFG bit 4) and reads the battery
 * (VBAT, 0x22, little-endian millivolts). Buttons KEY1 GPIO11, KEY2 GPIO12,
 * active low. The ST7789P3 135x240 panel is not driven yet. */
#include "board.h"
#include "board_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PM1_ADDR      0x6E
#define PM1_PWR_CFG   0x06
#define PM1_I2C_CFG   0x09
#define PM1_WDT_CNT   0x0A
#define PM1_GPIO_MODE 0x10
#define PM1_GPIO_FUNC0 0x16
#define PM1_VBAT_L    0x22
#define PM1_LED_EN    (1 << 4)

static const int buttons[] = { 11, 12 };
static int s_pmic_ok;

int board_init(void) {
    bc_buttons_init(buttons, 2);
    if (bc_i2c_init(47, 48, PM1_ADDR, 100000) != 0) return -1;
    uint8_t id[4];
    /* the first transfer after the PMIC idles can be NACKed (M5Unified retries) */
    for (int i = 0; i < 3 && !(s_pmic_ok = bc_i2c_read(0x00, id, 4) == 0); i++)
        vTaskDelay(1);
    if (!s_pmic_ok) return -2;
    bc_i2c_write8(PM1_I2C_CFG, 0x00);   /* no I2C idle sleep */
    bc_i2c_write8(PM1_WDT_CNT, 0x00);   /* no PMIC watchdog */
    return 0;
}
const char *board_name(void) { return "M5StickS3 (ESP32-S3)"; }
const char *board_mdns_host(void) { return "spinel-m5sticks3"; }
int board_led_init(void) { return s_pmic_ok ? board_led_set(0) : -1; }
int board_led_set(int on) {
    uint8_t v;
    if (!s_pmic_ok || bc_i2c_read(PM1_PWR_CFG, &v, 1) != 0) return -1;
    v = on ? (v | PM1_LED_EN) : (v & ~PM1_LED_EN);
    return bc_i2c_write8(PM1_PWR_CFG, v);
}
int board_battery_mv(void) {
    uint8_t b[2];
    if (!s_pmic_ok || bc_i2c_read(PM1_VBAT_L, b, 2) != 0) return -1;
    return b[0] | (b[1] << 8);
}
int board_button_count(void) { return bc_button_count(); }
int board_button_pressed(int idx) { return bc_button_pressed(idx); }
int board_has_lcd(void) { return 0; }
