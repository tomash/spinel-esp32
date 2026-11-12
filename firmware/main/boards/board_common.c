/* firmware/main/boards/board_common.c -- see board_common.h. */
#include "board_common.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/i2c_master.h"
#include "led_strip.h"

/* ---- GPIO LED ---- */
static int s_led_pin = -1, s_led_low = 0;
int bc_gpio_led_init(int pin, int active_low) {
    s_led_pin = pin; s_led_low = active_low;
    gpio_config_t c = { .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT };
    if (gpio_config(&c) != ESP_OK) return -1;
    return bc_gpio_led_set(0);
}
int bc_gpio_led_set(int on) {
    if (s_led_pin < 0) return -1;
    return gpio_set_level(s_led_pin, (on ? 1 : 0) ^ s_led_low) == ESP_OK ? 0 : -1;
}

/* ---- buttons ---- */
#define BC_MAX_BUTTONS 4
static int s_btn[BC_MAX_BUTTONS], s_nbtn = 0;
int bc_buttons_init(const int *pins, int n) {
    if (n > BC_MAX_BUTTONS) n = BC_MAX_BUTTONS;
    for (int i = 0; i < n; i++) {
        s_btn[i] = pins[i];
        gpio_config_t c = { .pin_bit_mask = 1ULL << pins[i], .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_ENABLE };
        /* input-only pins (classic ESP32 GPIO34-39) have no pull-up; the board
           carries its own, and gpio_config refuses the bit -- retry without */
        if (gpio_config(&c) != ESP_OK) { c.pull_up_en = GPIO_PULLUP_DISABLE; gpio_config(&c); }
    }
    s_nbtn = n;
    return 0;
}
int bc_button_count(void) { return s_nbtn; }
int bc_button_pressed(int idx) {
    if (idx < 0 || idx >= s_nbtn) return -1;
    return gpio_get_level(s_btn[idx]) == 0;
}

/* ---- ADC battery (the scheme wifi_status.rb used for T-OI / T-Display) ---- */
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static int s_div = 0;
int bc_adc_battery_init(adc_channel_t chan, int divider) {
    if (s_div) return 0;
    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1, .ulp_mode = ADC_ULP_MODE_DISABLE };
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) return -1;
    adc_oneshot_chan_cfg_t c = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
    if (adc_oneshot_config_channel(s_adc, chan, &c) != ESP_OK) return -2;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = chan,
        .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
    adc_cali_create_scheme_curve_fitting(&cc, &s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cc = { .unit_id = ADC_UNIT_1,
        .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12, .default_vref = 1100 };
    adc_cali_create_scheme_line_fitting(&cc, &s_cali);
#endif
    s_chan = chan; s_div = divider;
    return 0;
}
int bc_adc_battery_mv(void) {
    if (!s_div) return -1;
    int raw, mv;
    if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) return -1;
    if (!s_cali || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;
    return mv * s_div;
}

/* ---- WS2812 (one pixel) ---- */
static led_strip_handle_t s_strip;
int bc_ws2812_init(int pin) {
    led_strip_config_t sc = { .strip_gpio_num = pin, .max_leds = 1 };
    led_strip_rmt_config_t rc = { .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&sc, &rc, &s_strip) != ESP_OK) return -1;
    return bc_ws2812_set(0);
}
int bc_ws2812_set(int on) {
    if (!s_strip) return -1;
    if (!on) return led_strip_clear(s_strip) == ESP_OK ? 0 : -1;
    /* dim white: these LEDs are blinding at full scale */
    if (led_strip_set_pixel(s_strip, 0, 24, 24, 24) != ESP_OK) return -1;
    return led_strip_refresh(s_strip) == ESP_OK ? 0 : -1;
}

int bc_ws2812_rgb(int r, int g, int b) {
    if (!s_strip) return -1;
    if (led_strip_set_pixel(s_strip, 0, r & 0xFF, g & 0xFF, b & 0xFF) != ESP_OK) return -1;
    return led_strip_refresh(s_strip) == ESP_OK ? 0 : -1;
}

/* ---- defaults for optional board features; a board file overrides the
   ones its hardware has (weak symbols, resolved at link time) ---- */
#include "board.h"
#include "imu.h"
__attribute__((weak)) int board_led_rgb(int r, int g, int b) { (void)r; (void)g; (void)b; return -1; }
__attribute__((weak)) int board_imu_init(void) { return -1; }
__attribute__((weak)) int board_speaker_tone(int f, int ms, int v) { (void)f; (void)ms; (void)v; return -1; }
__attribute__((weak)) int board_mic_level(int ms) { (void)ms; return -1; }
__attribute__((weak)) int board_speaker_stop(void) { return -1; }
const char *board_imu_name(void) { return imu_name(); }
int board_imu_update(void) { return imu_update(); }
int board_imu_accel_mg(int axis) { return imu_accel_mg(axis); }
int board_imu_gyro_mdps(int axis) { return imu_gyro_mdps(axis); }

/* ---- I2C: one bus, several devices ---- */
#include <stdlib.h>
#include <string.h>
#define BC_MAX_I2C 4
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev[BC_MAX_I2C];
static int s_ndev;
int bc_i2c_init(int sda, int scl, uint8_t addr, uint32_t hz) {
    if (s_bus) return 0;
    i2c_master_bus_config_t bc = { .i2c_port = -1, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true };
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) { s_bus = NULL; return -1; }
    return bc_i2c_add(addr, hz) == 0 ? 0 : -2;
}
int bc_i2c_add(uint8_t addr, uint32_t hz) {
    if (!s_bus || s_ndev >= BC_MAX_I2C) return -1;
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr, .scl_speed_hz = hz };
    if (i2c_master_bus_add_device(s_bus, &dc, &s_dev[s_ndev]) != ESP_OK) return -1;
    return s_ndev++;
}
int bc_i2c_dev_write(int dev, uint8_t reg, const uint8_t *buf, int len) {
    if (dev < 0 || dev >= s_ndev) return -1;
    uint8_t small[17], *b = len < 17 ? small : malloc(len + 1);
    if (!b) return -1;
    b[0] = reg; memcpy(b + 1, buf, len);
    /* ~0.1 ms a byte at 100 kHz: an 8 KB burst (BMI270 config) needs ~0.8 s */
    int ms = 200 + len / 8;
    int rc = i2c_master_transmit(s_dev[dev], b, len + 1, ms) == ESP_OK ? 0 : -1;
    if (b != small) free(b);
    return rc;
}
int bc_i2c_dev_write8(int dev, uint8_t reg, uint8_t val) { return bc_i2c_dev_write(dev, reg, &val, 1); }
int bc_i2c_dev_read(int dev, uint8_t reg, uint8_t *buf, int len) {
    if (dev < 0 || dev >= s_ndev) return -1;
    return i2c_master_transmit_receive(s_dev[dev], &reg, 1, buf, len, 100) == ESP_OK ? 0 : -1;
}
int bc_i2c_dev_bits(int dev, uint8_t reg, uint8_t mask, int on) {
    uint8_t v;
    if (bc_i2c_dev_read(dev, reg, &v, 1) != 0) return -1;
    return bc_i2c_dev_write8(dev, reg, on ? (v | mask) : (v & ~mask));
}
int bc_i2c_write8(uint8_t reg, uint8_t val) { return bc_i2c_dev_write8(0, reg, val); }
int bc_i2c_read(uint8_t reg, uint8_t *buf, int len) { return bc_i2c_dev_read(0, reg, buf, len); }
