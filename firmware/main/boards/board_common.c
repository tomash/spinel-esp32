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

/* ---- I2C register helper ---- */
static i2c_master_dev_handle_t s_dev;
int bc_i2c_init(int sda, int scl, uint8_t addr, uint32_t hz) {
    if (s_dev) return 0;
    i2c_master_bus_config_t bc = { .i2c_port = -1, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) return -1;
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr, .scl_speed_hz = hz };
    return i2c_master_bus_add_device(bus, &dc, &s_dev) == ESP_OK ? 0 : -2;
}
int bc_i2c_write8(uint8_t reg, uint8_t val) {
    if (!s_dev) return -1;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK ? 0 : -1;
}
int bc_i2c_read(uint8_t reg, uint8_t *buf, int len) {
    if (!s_dev) return -1;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100) == ESP_OK ? 0 : -1;
}
