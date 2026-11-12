/* M5Stack M5StickS3 (ESP32-S3-PICO-1, 8 MB flash / 8 MB PSRAM).
 *
 * Power is held by an M5PM1 PMIC at 0x6E on I2C SDA47/SCL48. Its watchdog
 * and I2C idle sleep must be disabled at boot (what M5Unified's
 * M5PM1_Class::begin does), or the PMIC switches the board off -- which is
 * what a plain reset looked like before this board file existed. The PMIC
 * also owns the green LED (PWR_CFG bit 4) and reads the battery
 * (VBAT, 0x22, little-endian millivolts). Buttons KEY1 GPIO11, KEY2 GPIO12,
 * active low. Display: ST7789V2 135x240 (M5GFX board data: SCLK40 MOSI39
 * DC45 CS41 RST21, backlight GPIO38, window 52/40 like the T-Display). */
#include "board.h"
#include "board_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

#define PM1_ADDR      0x6E
#define PM1_PWR_CFG   0x06
#define PM1_I2C_CFG   0x09
#define PM1_WDT_CNT   0x0A
#define PM1_GPIO_MODE 0x10
#define PM1_GPIO_FUNC0 0x16
#define PM1_GPIO_OUT  0x11
#define PM1_GPIO_DRV  0x13
#define PM1_VBAT_L    0x22
#define PM1_LED_EN    (1 << 4)

static const int buttons[] = { 11, 12 };
static int s_pmic_ok;

static int pm1_bits(uint8_t reg, uint8_t mask, int on) {
    uint8_t v;
    if (bc_i2c_read(reg, &v, 1) != 0) return -1;
    return bc_i2c_write8(reg, on ? (v | mask) : (v & ~mask));
}

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
    /* PM1 GPIO2 = L3B_EN, the display's power rail: plain GPIO, output,
       push-pull, high (M5GFX pmic_ops::sticks3_power_on). Without it the
       panel takes its init commands and stays dark. */
    pm1_bits(PM1_GPIO_FUNC0, 0x04, 0);
    pm1_bits(PM1_GPIO_MODE, 0x04, 1);
    pm1_bits(PM1_GPIO_DRV, 0x04, 0);
    pm1_bits(PM1_GPIO_OUT, 0x04, 1);
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
int board_has_lcd(void) { return 1; }

static const board_display_t s_disp = {
    .host = SPI3_HOST, .sclk = 40, .mosi = 39, .cs = 41, .dc = 45, .rst = 21, .bl = 38,
    .width = 135, .height = 240, .gap_x = 52, .gap_y = 40, .gap_x_land = 40, .gap_y_land = 53,
    .mirror_x = 0, .mirror_y = 0, .mirror_x_land = 1, .mirror_y_land = 0,
    .invert = 1, .bgr = 0, .pclk_hz = 40 * 1000 * 1000,
};
const board_display_t *board_display(void) { return &s_disp; }
int board_display_power(int on) { (void)on; return 0; }
#include "imu.h"
int board_imu_init(void) { return s_pmic_ok ? imu_init(IMU_BMI270, 0x68) : -1; }
/* IR: LED on G46, receiver on G42 (M5Stack StickS3 pin map). M5Stack: IR
   needs the PMIC's 5 V output on, and the speaker amplifier (PM1 GPIO3) off,
   or its supply noise blocks reception. */
void board_ir_pins(int *tx, int *rx) { *tx = 46; *rx = 42; }
int board_ir_prepare(void) {
    if (!s_pmic_ok) return -1;
    pm1_bits(PM1_PWR_CFG, 1 << 3, 1);       /* BOOST_EN: 5 V out */
    pm1_bits(PM1_GPIO_FUNC0, 1 << 3, 0);    /* GPIO3 (amp) as plain GPIO */
    pm1_bits(PM1_GPIO_MODE, 1 << 3, 1);     /*   output */
    pm1_bits(PM1_GPIO_DRV, 1 << 3, 0);      /*   push-pull */
    pm1_bits(PM1_GPIO_OUT, 1 << 3, 0);      /*   low: amplifier off */
    return 0;
}
/* Audio: ES8311 codec at 0x18 on the PMIC's bus; the speaker amplifier is
   PM1 GPIO3 (high = on), switched only around a tone so it does not disturb
   IR reception. On battery (no VIN), volume is held to 75 %. */
#include "audio.h"
static int pm1_on_usb(void) {
    uint8_t src;
    return bc_i2c_read(0x04, &src, 1) == 0 && (src & 0x03);   /* PWR_SRC: vin | vinout */
}
int board_speaker_tone(int freq_hz, int ms, int volume_pct) {
    if (!s_pmic_ok || es8311_attach(0x18) != 0) return -1;
    if (!pm1_on_usb() && volume_pct > 75) volume_pct = 75;
    pm1_bits(PM1_GPIO_FUNC0, 1 << 3, 0);
    pm1_bits(PM1_GPIO_MODE, 1 << 3, 1);
    pm1_bits(PM1_GPIO_DRV, 1 << 3, 0);
    pm1_bits(PM1_GPIO_OUT, 1 << 3, 1);      /* amplifier on (stays on until stop) */
    return es8311_tone(freq_hz, ms, volume_pct);
}
int board_speaker_stop(void) {
    es8311_stop();
    return s_pmic_ok ? pm1_bits(PM1_GPIO_OUT, 1 << 3, 0) : -1;   /* amplifier off */
}
int board_mic_level(int ms) {
    if (!s_pmic_ok || es8311_attach(0x18) != 0) return -1;
    return es8311_mic_level(ms);
}
