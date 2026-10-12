# T-Display LCD (ST7789V)

The `feature/tdisplay-st7789` branch adds a small Ruby-facing drawing API
for the built-in 135×240 ST7789V panel. It uses Espressif's `esp_lcd`
ST7789 implementation shipped with the project's ESP-IDF v5.5.5; no
third-party controller driver is vendored.

## Ruby API

See `app/display_tdisplay.rb` for a working demo:

```ruby
TDisplay.esp32_display_init
TDisplay.esp32_display_clear(0x0000)
TDisplay.esp32_display_rect(0, 0, 135, 30, 0x001F, 1)
TDisplay.esp32_display_text("HELLO", 8, 40, 0xFFFF, 0x0000, 2)
```

Colors are RGB565 integers. The rectangle call takes `x, y, width, height,
color, filled` (filled is 0 or 1). Text takes `text, x, y, foreground,
background, scale`; scale is 1–4, with a compact 5×7 font covering uppercase
ASCII, digits, and common punctuation. Coordinates are clipped to the screen.
The API returns ESP-IDF error codes; the current demo ignores them for
brevity.

Drawing is deliberately synchronous from the Ruby caller's perspective.
Each scanline is transferred through ESP-IDF's queued SPI LCD API using one
DMA-capable line buffer (270 bytes), and the next draw waits for the prior
transfer callback. There is no 64.8 KB framebuffer.

## Board wiring and panel settings

| Signal | GPIO |
| --- | ---: |
| SPI MOSI | 19 |
| SPI SCLK | 18 |
| LCD CS | 5 |
| LCD D/C | 16 |
| LCD reset | 23 |
| Backlight (active high) | 4 |

The panel is configured for RGB565 at 40 MHz, gap `(52, 40)`, and color
inversion. The gaps match common TTGO T-Display ST7789V configurations;
rotation, exact edge alignment, and color order should be confirmed visually
on the particular board revision. If the picture is shifted, upside down, or
colors are inverted, adjust `esp_lcd_panel_set_gap`, `esp_lcd_panel_swap_xy`,
`esp_lcd_panel_mirror`, `esp_lcd_panel_invert_color`, or element order in
`firmware/main/tdisplay.c`.

## Build / flash

Use the T-Display's serial port (on the session hardware this was
`/dev/ttyUSB1`; serial enumeration can change):

```sh
SPINEL_ESP_PORT=/dev/ttyUSB1 bash tools/build-app.sh \
  app/display_tdisplay.rb --target=esp32 --flash
SPINEL_ESP_PORT=/dev/ttyUSB1 SPINEL_ESP_CAPTURE=15 \
  python3 tools/monitor.py
```

Expected serial output includes `T-Display initialized and demo rendered`
and `main() returned 0`. The display itself must be checked visually to
confirm orientation, offsets, color order, and inversion.
