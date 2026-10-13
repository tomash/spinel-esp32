# app/buttons_tdisplay.rb -- drive the LCD text color with the onboard buttons.
#
# The LilyGO T-Display (classic ESP32) has two onboard push buttons, both
# active-low through external pull-ups and not broken out to the header:
#   index 0 = GPIO0  (left / BOOT button)
#   index 1 = GPIO35 (right button; an input-only pin, so its idle level
#                     comes entirely from the board's pull-up resistor)
#
# The C side (firmware/main/tdisplay.c) does esp32_buttons_init() once and
# exposes esp32_button_pressed(idx) -> 1/0 (level==0 means pressed). This
# demo polls both buttons every 20 ms with a one-shot edge detector: the
# left button advances the text color through a palette, the right button
# steps back. The big color name plus a swatch bar redraw on every change;
# the title bar and hint lines are drawn once.
#
# Colors are RGB565 (0xF800 red, 0x07E0 green, 0x001F blue, 0xFFFF white).

module TDisplay
  # esp32_delay_ms is a plain FreeRTOS sleep wrapper; the panel/button
  # functions themselves live in firmware/main/tdisplay.c.
  ffi_source <<~C
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    int esp32_delay_ms(int ms) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
        return 0;
    }
  C

  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_width,  [], :int
  ffi_func :esp32_display_height, [], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_rect,  [:int, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
  ffi_func :esp32_buttons_init,  [], :int
  ffi_func :esp32_button_pressed, [:int], :int
  ffi_func :esp32_delay_ms, [:int], :int, blocking: true
end

W = 240
H = 135

# Palette index order is the cycle order; names are drawn on the panel, so
# keep them short enough for scale 3 (18 px/char -> 9 chars fit in 240 px).
COLORS = [0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFD20]
NAMES  = ["WHITE", "RED", "GREEN", "BLUE", "YELLOW", "MAGENTA", "CYAN", "ORANGE"]

def draw_chrome
  TDisplay.esp32_display_rect(0, 0, W, 24, 0x001F, 1)       # blue title bar
  TDisplay.esp32_display_text("T-DISPLAY / BUTTONS", 6, 7, 0xFFFF, 0x001F, 1)
  TDisplay.esp32_display_text("LEFT: NEXT COLOR", 6, 104, 0x07E0, 0x0000, 1)
  TDisplay.esp32_display_text("RIGHT: PREV COLOR", 6, 118, 0x07E0, 0x0000, 1)
end

def draw_color(idx, color)
  # Wipe only the middle band so the chrome does not flicker.
  TDisplay.esp32_display_rect(0, 30, W, 68, 0x0000, 1)
  TDisplay.esp32_display_text(NAMES[idx], 8, 40, color, 0x0000, 3)
  TDisplay.esp32_display_rect(8, 78, W - 16, 12, color, 1)  # swatch bar
end

TDisplay.esp32_display_init
TDisplay.esp32_display_set_orientation(1)   # landscape 240x135
TDisplay.esp32_buttons_init
TDisplay.esp32_display_clear(0x0000)
draw_chrome

idx = 0
draw_color(idx, COLORS[idx])
puts "T-Display buttons demo up (#{W}x#{H}); palette #{COLORS.size} colors"
puts "Initial color: #{NAMES[idx]} (#{COLORS[idx]})"

# Edge detection: buttons idle high; a falling edge (1 -> 0) is a press.
# No repeats while held -- press again to advance.
prev0 = 1
prev1 = 1
presses = 0
while true
  b0 = TDisplay.esp32_button_pressed(0)
  b1 = TDisplay.esp32_button_pressed(1)

  if b0 == 0 && prev0 == 1
    idx = (idx + 1) % COLORS.size
    draw_color(idx, COLORS[idx])
    presses += 1
    puts "BTN0 (GPIO0) pressed -> #{NAMES[idx]}"
  end
  if b1 == 0 && prev1 == 1
    idx = (idx + COLORS.size - 1) % COLORS.size   # step back, wrapping
    draw_color(idx, COLORS[idx])
    presses += 1
    puts "BTN1 (GPIO35) pressed -> #{NAMES[idx]}"
  end

  prev0 = b0
  prev1 = b1
  TDisplay.esp32_delay_ms(20)
end
