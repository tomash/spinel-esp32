# app/display_tdisplay.rb -- Draw a Ruby-rendered demo on the built-in LCD.
#
# API calls are implemented in firmware/main/tdisplay.c and use ESP-IDF's
# esp_lcd ST7789V panel driver. Colors use RGB565 (0xF800 red, 0x07E0
# green, 0x001F blue, 0xFFFF white, 0x0000 black).

module TDisplay
  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_rect,  [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
end

# Initialize panel, fill background, then draw a simple status screen.
TDisplay.esp32_display_init
TDisplay.esp32_display_clear(0x0000)
TDisplay.esp32_display_rect(0, 0, 135, 30, 0x001F, 1)
TDisplay.esp32_display_text("SPINEL ESP32", 6, 9, 0xFFFF, 0x001F, 1)
TDisplay.esp32_display_text("XTENSA READY", 8, 55, 0x07E0, 0x0000, 2)
TDisplay.esp32_display_rect(8, 105, 119, 4, 0xF800, 1)
TDisplay.esp32_display_text("Ruby draws pixels", 8, 125, 0xFFFF, 0x0000, 1)
TDisplay.esp32_display_text("ST7789 / SPI", 8, 145, 0xFFE0, 0x0000, 1)
puts "T-Display initialized and demo rendered"
