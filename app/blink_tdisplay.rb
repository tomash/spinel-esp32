# app/blink_tdisplay.rb -- M3 demo 1, LilyGO T-Display (classic ESP32) edition.
#
# The T-Display v1.1 has NO user LED. GPIO4 is the backlight-enable pin of
# the ST7789 display: toggling it blinks the *screen* on/off, which is the
# board's most visible indicator without any wiring. (The two exposed
# header GPIOs 22/27 would need a jumper LED; the display itself stays out
# of scope for this milestone.)
#
# Same FFI surface as blink.rb -- only the pin differs.

module ESP32
  ffi_source <<~C
    #include "driver/gpio.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"

    /* T-Display backlight enable on GPIO4, active-high; gpio_config once. */
    int esp32_led_init(int pin) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        return gpio_config(&cfg);
    }
    int esp32_led_write(int pin, int level) {
        return gpio_set_level(pin, level ? 1 : 0);
    }
    int esp32_delay_ms(int ms) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
        return 0;
    }
  C

  ffi_func :esp32_led_init,  [:int], :int
  ffi_func :esp32_led_write, [:int, :int], :int
  ffi_func :esp32_delay_ms,  [:int], :int, blocking: true
end

LED_PIN = 4   # T-Display backlight enable
BLINKS = 10

ESP32.esp32_led_init(LED_PIN)
puts "Blinking T-Display backlight (GPIO#{LED_PIN}) #{BLINKS} times:"
n = 0
while n < BLINKS
  ESP32.esp32_led_write(LED_PIN, 1)
  ESP32.esp32_delay_ms(200)
  ESP32.esp32_led_write(LED_PIN, 0)
  ESP32.esp32_delay_ms(200)
  n += 1
end
puts "Blink done. If the screen backlight flashed 10 times, GPIO output from Ruby works."
