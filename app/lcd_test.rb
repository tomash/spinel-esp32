# app/lcd_test.rb -- display bring-up pattern for any board with a panel.
#
# Landscape, five labelled bars: pure red, green, blue, white, black (RGB565
# 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000), plus the board name on top. What
# each bar really shows tells which panel flag is off: red<->blue swapped is
# the colour order (bgr); every colour turned negative is inversion; text
# mirrored or upside down is the mirror flags (firmware/main/boards/<board>.c).

module LCD
  ffi_func :board_name,                    [], :str
  ffi_func :esp32_display_init,            [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_width,           [], :int
  ffi_func :esp32_display_height,          [], :int
  ffi_func :esp32_display_clear,           [:int], :int
  ffi_func :esp32_display_rect,            [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,            [:str, :int, :int, :int, :int, :int], :int
end

rc = LCD.esp32_display_init
puts "display init: #{rc}"
if rc == 0
  LCD.esp32_display_set_orientation(1)
  w = LCD.esp32_display_width
  h = LCD.esp32_display_height
  puts "display: #{w}x#{h} landscape"
  LCD.esp32_display_clear(0x0000)
  LCD.esp32_display_text(LCD.board_name.upcase[0, (w / 6) - 1], 2, 2, 0xFFFF, 0x0000, 1)
  colors = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000]
  labels = ["R", "G", "B", "W", "K"]
  bw = w / 5
  top = 14
  i = 0
  while i < 5
    LCD.esp32_display_rect(i * bw, top, bw, h - top - 12, colors[i], 1)
    LCD.esp32_display_rect(i * bw, top, bw, h - top - 12, 0x8410, 0)   # grey outline
    LCD.esp32_display_text(labels[i], i * bw + bw / 2 - 3, h - 10, 0xFFFF, 0x0000, 1)
    i += 1
  end
  puts "pattern drawn: R G B W K"
end
