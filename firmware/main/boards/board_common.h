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

/* One I2C master bus per board (the PMIC's); register helpers return 0 on
 * success, negative on error. */
int  bc_i2c_init(int sda, int scl, uint8_t addr, uint32_t hz);
int  bc_i2c_write8(uint8_t reg, uint8_t val);
int  bc_i2c_read(uint8_t reg, uint8_t *buf, int len);
