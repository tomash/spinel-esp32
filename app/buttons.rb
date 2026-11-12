# app/buttons.rb -- the board's buttons, from Ruby, on any board.
#
# Polls every button (board_button_pressed), debounces, and prints each press
# and release with how long it was held. A press also answers on the LED: a
# colour step on boards with an RGB LED (M5Stamp C3, ESP32-C6), a toggle on
# the others.

module Board
  ffi_func :board_name,           [], :str
  ffi_func :board_button_count,   [], :int
  ffi_func :board_button_pressed, [:int], :int
  ffi_func :board_led_init,       [], :int
  ffi_func :board_led_set,        [:int], :int
  ffi_func :board_led_rgb,        [:int, :int, :int], :int
  ffi_source <<~C
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "esp_timer.h"
    int btn_delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS((uint32_t)ms)); return 0; }
    int btn_millis(void) { return (int)(esp_timer_get_time() / 1000); }
  C
  ffi_func :btn_delay_ms, [:int], :int, blocking: true
  ffi_func :btn_millis,   [], :int
end

COLORS = [[40, 0, 0], [0, 40, 0], [0, 0, 40], [40, 40, 0], [0, 40, 40], [40, 0, 40], [40, 40, 40]]
NAMES  = ["red", "green", "blue", "yellow", "cyan", "magenta", "white"]

n = Board.board_button_count
puts "board:   #{Board.board_name}"
puts "buttons: #{n} -- press them"
Board.board_led_init
rgb = Board.board_led_rgb(0, 0, 0) == 0
state = []
since = []
i = 0
while i < n
  state << 0
  since << 0
  i += 1
end
presses = 0
led = 0
while true
  i = 0
  while i < n
    now = Board.board_button_pressed(i) == 1 ? 1 : 0
    if now != state[i]
      Board.btn_delay_ms(15)                       # debounce: still the same?
      if (Board.board_button_pressed(i) == 1 ? 1 : 0) == now
        t = Board.btn_millis
        if now == 1
          presses += 1
          puts "button #{i}: pressed (#{presses} so far)"
          if rgb
            c = COLORS[presses % COLORS.size]
            Board.board_led_rgb(c[0], c[1], c[2])
            puts "  led -> #{NAMES[presses % NAMES.size]}"
          else
            led = 1 - led
            Board.board_led_set(led)
          end
        else
          puts "button #{i}: released after #{t - since[i]} ms"
        end
        since[i] = t
        state[i] = now
      end
    end
    i += 1
  end
  Board.btn_delay_ms(10)
end
