# app/board_info.rb -- the board layer from Ruby, on any board.
#
# Prints what firmware/main/boards/<board>.c reports (name, mDNS host,
# battery, buttons) and blinks the board's LED, whatever that LED is: a GPIO
# LED, a WS2812 RGB pixel, the PMIC's LED or a backlight. The same file runs
# on every board; build with SPINEL_BOARD=<board> (see firmware/spinel_target.cmake).

module Board
  ffi_func :board_name,           [], :str
  ffi_func :board_mdns_host,      [], :str
  ffi_func :board_led_init,       [], :int
  ffi_func :board_led_set,        [:int], :int
  ffi_func :board_battery_mv,     [], :int
  ffi_func :board_button_count,   [], :int
  ffi_func :board_button_pressed, [:int], :int
  ffi_func :board_has_lcd,        [], :int

  ffi_source <<~C
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    int board_delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS((uint32_t)ms)); return 0; }
  C
  ffi_func :board_delay_ms, [:int], :int, blocking: true
end

puts "board:   #{Board.board_name}"
puts "mdns:    #{Board.board_mdns_host}.local"
mv = Board.board_battery_mv
puts(mv >= 0 ? "battery: #{mv} mV" : "battery: none")
n = Board.board_button_count
states = []
i = 0
while i < n
  states << (Board.board_button_pressed(i) == 1 ? "down" : "up")
  i += 1
end
puts "buttons: #{n}#{n > 0 ? " (#{states.join(", ")})" : ""}"
puts "lcd:     #{Board.board_has_lcd == 1 ? "yes" : "no"}"

rc = Board.board_led_init
if rc != 0
  puts "led:     none (init #{rc})"
else
  puts "led:     blinking 5 times"
  5.times do
    Board.board_led_set(1)
    Board.board_delay_ms(250)
    Board.board_led_set(0)
    Board.board_delay_ms(250)
  end
end
puts "board_info done"
