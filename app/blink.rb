# app/blink.rb -- M3 demo 1: GPIO output. Blinks the onboard LED from Ruby.
#
# T-OI Plus v1.2: the "chicken" LED is on GPIO3, active-high. This demo
# configures the pin and toggles it in a loop, with the loop count and
# timing visible over UART.
#
# The ESP-IDF driver surface is reached through spinel's FFI DSL:
# ffi_source embeds a tiny C adapter into the generated TU, and ffi_func
# declares the typed boundary. Everything below is plain Ruby.

module ESP32
  ffi_source <<~C
    #include "driver/gpio.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"

    /* LED on GPIO3, active-high; gpio_config once, then level writes. */
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

LED_PIN = 3
BLINKS = 10

ESP32.esp32_led_init(LED_PIN)
puts "Blinking GPIO#{LED_PIN} #{BLINKS} times (LED is active-high):"
n = 0
while n < BLINKS
  ESP32.esp32_led_write(LED_PIN, 1)
  ESP32.esp32_delay_ms(200)
  ESP32.esp32_led_write(LED_PIN, 0)
  ESP32.esp32_delay_ms(200)
  n += 1
end
puts "Blink done. If the LED flashed 10 times, GPIO output from Ruby works."
