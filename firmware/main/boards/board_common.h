/* firmware/main/boards/board_common.h -- building blocks the board files
 * share: a GPIO LED, active-low buttons, an ADC battery divider, an
 * addressable RGB LED (WS2812 over RMT, via espressif/led_strip) and a small
 * I2C register helper for PMICs. Each board file picks pins and composes. */
#pragma once
#include <stdint.h>
#include "soc/soc_caps.h"
#include "hal/adc_types.h"

int  bc_gpio_led_init(int pin, int active_low);
int  bc_gpio_led_set(int on);

int  bc_buttons_init(const int *pins, int n);   /* active low, pull-up */
int  bc_button_count(void);
int  bc_button_pressed(int idx);

/* Battery on an ADC1 channel behind a 1:divider resistor divider; answers
 * the cell voltage in mV (pin mV x divider), or -1. Calibration: curve
 * fitting where the chip has it, else line fitting (classic ESP32). */
int  bc_adc_battery_init(adc_channel_t chan, int divider);
int  bc_adc_battery_mv(void);

int  bc_ws2812_init(int pin);
int  bc_ws2812_set(int on);
int  bc_ws2812_rgb(int r, int g, int b);

/* One I2C master bus per board, several devices on it. bc_i2c_init makes the
 * bus and device 0 (the PMIC); bc_i2c_add puts another device on the same bus
 * (an IMU, a codec) and answers its index. The plain helpers talk to device
 * 0; the _dev ones take an index. 0 on success, negative on error. */
int  bc_i2c_init(int sda, int scl, uint8_t addr, uint32_t hz);
int  bc_i2c_add(uint8_t addr, uint32_t hz);
int  bc_i2c_write8(uint8_t reg, uint8_t val);
int  bc_i2c_read(uint8_t reg, uint8_t *buf, int len);
int  bc_i2c_dev_write8(int dev, uint8_t reg, uint8_t val);
int  bc_i2c_dev_write(int dev, uint8_t reg, const uint8_t *buf, int len);
int  bc_i2c_dev_read(int dev, uint8_t reg, uint8_t *buf, int len);
int  bc_i2c_dev_bits(int dev, uint8_t reg, uint8_t mask, int on);
