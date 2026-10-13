# T-Display LCD (ST7789V)

The `feature/tdisplay-st7789` branch adds a small Ruby-facing drawing API
for the built-in 135×240 ST7789V panel. It uses Espressif's `esp_lcd`
ST7789 implementation shipped with the project's ESP-IDF v5.5.5; no
third-party controller driver is vendored.

The panel is drawn in **landscape 240×135** orientation:
`esp32_display_set_orientation(1)` flips the MADCTL `MV` bit so the
logical x axis runs along the long glass edge (and swaps the address-window
offset to `(40, 52)`), while `esp32_display_width`/`_height` report the
logical size. Landscape gives 40 usable characters per line at scale 1,
enough for the full `http://<host>.local` status line without wrapping.

## Ruby API

See `app/display_tdisplay.rb` for a working demo:

```ruby
TDisplay.esp32_display_init
TDisplay.esp32_display_set_orientation(1)   # landscape 240x135
TDisplay.esp32_display_clear(0x0000)
TDisplay.esp32_display_rect(0, 0, 240, 26, 0x001F, 1)
TDisplay.esp32_display_text("HELLO", 8, 40, 0xFFFF, 0x0000, 2)
```

Colors are RGB565 integers. The rectangle call takes `x, y, width, height,
color, filled` (filled is 0 or 1). Text takes `text, x, y, foreground,
background, scale`; scale is 1–4, with a compact 5×7 font covering uppercase
ASCII, digits, and common punctuation. Coordinates are clipped to the screen.
`esp32_display_set_orientation(0)` restores portrait 135×240. The API returns
ESP-IDF error codes; the current demo ignores them for brevity.

Drawing is deliberately synchronous from the Ruby caller's perspective.
Each scanline is transferred through ESP-IDF's queued SPI LCD API using one
DMA-capable line buffer (480 bytes, sized for the 240-px landscape row), and
the next draw waits for the prior transfer callback. There is no 64.8 KB
framebuffer.

## Board wiring and panel settings

| Signal | GPIO |
| --- | ---: |
| SPI MOSI | 19 |
| SPI SCLK | 18 |
| LCD CS | 5 |
| LCD D/C | 16 |
| LCD reset | 23 |
| Backlight (active high) | 4 |

The panel is configured for RGB565 at 40 MHz, gap `(52, 40)` portrait /
`(40, 52)` landscape, and color inversion. The gaps match common TTGO
T-Display ST7789V configurations; rotation, exact edge alignment, and color
order should be confirmed visually on the particular board revision. If the
picture is shifted, upside down, or colors are inverted, adjust
`esp_lcd_panel_set_gap`, `esp_lcd_panel_swap_xy`, `esp_lcd_panel_mirror`,
`esp_lcd_panel_invert_color`, or element order in `firmware/main/tdisplay.c`.

## Onboard buttons

The T-Display (classic ESP32) has two tactile buttons on the board, beside
the USB connector. Neither is broken out on the header, and both are
**active-low with external pull-up resistors**:

| Index | GPIO | Function |
| ---: | ---: | --- |
| 0 | 0 | left / `BOOT` button (also the strapping pin) |
| 1 | 35 | right button |

GPIO35 is an **input-only** pin with no internal pull-up, so the board's
resistor is the only thing holding it high when released. The driver
therefore configures plain `GPIO_MODE_INPUT` inputs with **no internal
pulls** and reports a press when the level reads `0`:

```ruby
TDisplay.esp32_buttons_init          # idempotent; call once
if TDisplay.esp32_button_pressed(0) == 1
  # left button is down right now
end
```

`esp32_buttons_init` is a one-shot: it programs the GPIO configuration on
the first call and returns `ESP_OK` immediately on later calls.
`esp32_button_pressed(idx)` returns `1` while the button is held, `0` when
released, and `ESP_ERR_INVALID_ARG` for an index outside `[0, 1]`. The API
does no debouncing or edge detection — poll it and remember the previous
level yourself (see the demo below).

`app/buttons_tdisplay.rb` is a complete demo: it draws a title bar, polls
both buttons every 20 ms, and uses falling-edge detection so each physical
press advances the colour once (left = next, right = previous) through an
8-colour RGB565 palette, printing each change to the serial console:

```sh
SPINEL_ESP_PORT=/dev/ttyUSB1 bash tools/build-app.sh \
  app/buttons_tdisplay.rb --target=esp32 --flash
```

Expected serial output:

```
T-Display buttons demo up (240x135); palette 8 colors
Initial color: WHITE (65535)
BTN0 (GPIO0) pressed -> RED
```

The demo is T-Display-specific (it does not gate on
`esp32_board_has_lcd()`); the button symbols are linked into both targets,
so the C3 build is unaffected.

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
