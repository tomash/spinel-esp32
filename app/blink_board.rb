# app/blink_board.rb -- blink the board's LED through the board layer, on any
# board (ESP32 or Pico): no SDK call in the app, Ruby's own sleep for timing.
module Board
  ffi_func :board_name,     [], :str
  ffi_func :board_led_init, [], :int
  ffi_func :board_led_set,  [:int], :int
end

BLINKS = 10
puts "#{Board.board_name}: blinking the LED #{BLINKS} times"
rc = Board.board_led_init
puts "led init: #{rc}"
n = 0
while n < BLINKS
  Board.board_led_set(1)
  sleep 0.2
  Board.board_led_set(0)
  sleep 0.2
  n += 1
  puts "blink #{n}"
end
puts "Blink done."
