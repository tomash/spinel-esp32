# app/display_tdisplay.rb -- Draw a Ruby-rendered demo on the built-in LCD.
#
# API calls are implemented in firmware/main/tdisplay.c and use ESP-IDF's
# esp_lcd ST7789V panel driver. Colors use RGB565 (0xF800 red, 0x07E0
# green, 0x001F blue, 0xFFFF white, 0x0000 black).
#
# The panel is driven in landscape (240x135): set_orientation(1) flips the
# MADCTL MV bit so x runs along the long glass edge. The live status screen
# in wifi_status.rb uses the same call.

module TDisplay
  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_width,  [], :int
  ffi_func :esp32_display_height, [], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_rect,  [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
end

# Initialize panel in landscape, fill background, draw a demo screen.
TDisplay.esp32_display_init
TDisplay.esp32_display_set_orientation(1)
w = TDisplay.esp32_display_width    # 240
h = TDisplay.esp32_display_height   # 135
TDisplay.esp32_display_clear(0x0000)
TDisplay.esp32_display_rect(0, 0, w, 26, 0x001F, 1)
TDisplay.esp32_display_text("SPINEL ESP32", 6, 8, 0xFFFF, 0x001F, 1)
TDisplay.esp32_display_text("XTENSA READY", 8, 46, 0x07E0, 0x0000, 2)
TDisplay.esp32_display_rect(8, 90, w - 16, 4, 0xF800, 1)
TDisplay.esp32_display_text("Ruby draws pixels", 8, 106, 0xFFFF, 0x0000, 1)
TDisplay.esp32_display_text("ST7789 / SPI 240x135", 8, 120, 0xFFE0, 0x0000, 1)
puts "T-Display initialized and demo rendered (#{w}x#{h})"
