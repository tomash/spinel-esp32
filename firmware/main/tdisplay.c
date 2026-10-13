/* Minimal Ruby-facing drawing API for the LilyGO T-Display ST7789V. */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TDISPLAY_WIDTH_PORTRAIT  135
#define TDISPLAY_HEIGHT_PORTRAIT 240
#define TDISPLAY_WIDTH_LANDSCAPE  240
#define TDISPLAY_HEIGHT_LANDSCAPE 135
#define TDISPLAY_MAX_ROW          TDISPLAY_WIDTH_LANDSCAPE
#define TDISPLAY_MOSI   19
#define TDISPLAY_SCLK   18
#define TDISPLAY_CS     5
#define TDISPLAY_DC     16
#define TDISPLAY_RST    23
#define TDISPLAY_BL     4

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_tx_done;
static bool s_tx_pending;
static DMA_ATTR uint16_t s_row[TDISPLAY_MAX_ROW];

/* Logical (post-rotation) panel size. Defaults to the as-mounted portrait
   window; esp32_display_set_orientation(1) switches to landscape by
   transposing the scan direction (MADCTL MV) and swapping the address
   window offset, so draw_bitmap coordinates are what the caller thinks. */
static int s_width  = TDISPLAY_WIDTH_PORTRAIT;
static int s_height = TDISPLAY_HEIGHT_PORTRAIT;

static bool color_transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    (void)io;
    (void)event;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static void wait_for_transfer(void)
{
    if (s_tx_pending) {
        xSemaphoreTake(s_tx_done, portMAX_DELAY);
        s_tx_pending = false;
    }
}

static esp_err_t draw_row(int x, int y, int width)
{
    wait_for_transfer();
    /* touch the byte after the last pixel: esp_lcd_panel_draw_bitmap walks
       the buffer as uint32_t on the last word of len bytes (up to 3 bytes
       past), which the SPI master then DMAs from -- keep it inside s_row */
    ((volatile unsigned char *)s_row)[width * 2] = 0;
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + width, y + 1, s_row);
    if (err == ESP_OK) {
        s_tx_pending = true;
        wait_for_transfer();
    }
    return err;
}

int esp32_display_init(void)
{
    if (s_panel) return ESP_OK;

    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << TDISPLAY_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&backlight);
    if (err != ESP_OK) return err;

    s_tx_done = xSemaphoreCreateBinary();
    if (!s_tx_done) return ESP_ERR_NO_MEM;

    spi_bus_config_t bus = {
        .sclk_io_num = TDISPLAY_SCLK,
        .mosi_io_num = TDISPLAY_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(s_row) + 1,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TDISPLAY_DC,
        .cs_gpio_num = TDISPLAY_CS,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_transfer_done,
        .user_ctx = s_tx_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io);
    if (err != ESP_OK) return err;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TDISPLAY_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_config, &s_panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) return err;

    /* T-Display glass exposes a 135x240 window inside the ST7789 RAM, seen
       through a 52/40 address-window offset in the as-mounted orientation. */
    err = esp_lcd_panel_set_gap(s_panel, 52, 40);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_invert_color(s_panel, true);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) return err;
    return gpio_set_level(TDISPLAY_BL, 1);
}

/* Rotate the logical drawing surface. 0 = portrait (135x240, as mounted),
   1 = landscape (240x135). Flips the MADCTL MV bit and, with it, which
   address-window offset (x_gap/y_gap) applies to the short glass edge. */
int esp32_display_set_orientation(int landscape)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    wait_for_transfer();
    esp_err_t err = esp_lcd_panel_swap_xy(s_panel, landscape != 0);
    if (err != ESP_OK) return err;
    if (landscape) {
        err = esp_lcd_panel_set_gap(s_panel, 40, 52);
        if (err != ESP_OK) return err;
        s_width  = TDISPLAY_WIDTH_LANDSCAPE;
        s_height = TDISPLAY_HEIGHT_LANDSCAPE;
    } else {
        err = esp_lcd_panel_set_gap(s_panel, 52, 40);
        if (err != ESP_OK) return err;
        s_width  = TDISPLAY_WIDTH_PORTRAIT;
        s_height = TDISPLAY_HEIGHT_PORTRAIT;
    }
    return ESP_OK;
}

int esp32_display_width(void)  { return s_width; }
int esp32_display_height(void) { return s_height; }

int esp32_display_clear(int color)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    wait_for_transfer();
    for (int x = 0; x < s_width; x++) s_row[x] = (uint16_t)color;
    for (int y = 0; y < s_height; y++) {
        esp_err_t err = draw_row(0, y, s_width);
        if (err != ESP_OK) return err;
    }
    wait_for_transfer();
    return ESP_OK;
}

int esp32_display_rect(int x, int y, int width, int height, int color, int filled)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    wait_for_transfer();
    if (width <= 0 || height <= 0) return ESP_ERR_INVALID_ARG;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > s_width) x1 = s_width;
    if (y1 > s_height) y1 = s_height;
    if (x0 >= x1 || y0 >= y1) return ESP_OK;

    for (int row = y0; row < y1; row++) {
        if (!filled && row != y0 && row != y1 - 1) continue;
        for (int col = 0; col < x1 - x0; col++) s_row[col] = (uint16_t)color;
        if (!filled && x1 - x0 > 2) {
            /* Draw outline edges only; interior pixels are not submitted. */
            esp_err_t err = draw_row(x0, row, 1);
            if (err != ESP_OK) return err;
            err = draw_row(x1 - 1, row, 1);
            if (err != ESP_OK) return err;
        } else {
            esp_err_t err = draw_row(x0, row, x1 - x0);
            if (err != ESP_OK) return err;
        }
    }
    return ESP_OK;
}

/* Compact 5x7 column font for uppercase Latin letters, digits and punctuation. */
static const uint8_t font[][5] = {
    [' '-32]={0,0,0,0,0}, ['!'-32]={0,0,0x5f,0,0}, ['-'-32]={8,8,8,8,8}, ['.'-32]={0,0x60,0x60,0,0}, [':'-32]={0,0x36,0x36,0,0}, ['/'-32]={0x20,0x10,8,4,2}, ['?'-32]={2,1,0x51,9,6},
    ['0'-32]={0x3e,0x51,0x49,0x45,0x3e}, ['1'-32]={0,0x42,0x7f,0x40,0}, ['2'-32]={0x42,0x61,0x51,0x49,0x46}, ['3'-32]={0x21,0x41,0x45,0x4b,0x31},
    ['4'-32]={0x18,0x14,0x12,0x7f,0x10}, ['5'-32]={0x27,0x45,0x45,0x45,0x39}, ['6'-32]={0x3c,0x4a,0x49,0x49,0x30}, ['7'-32]={1,0x71,9,5,3},
    ['8'-32]={0x36,0x49,0x49,0x49,0x36}, ['9'-32]={6,0x49,0x49,0x29,0x1e},
    ['A'-32]={0x7e,0x11,0x11,0x11,0x7e}, ['B'-32]={0x7f,0x49,0x49,0x49,0x36}, ['C'-32]={0x3e,0x41,0x41,0x41,0x22}, ['D'-32]={0x7f,0x41,0x41,0x22,0x1c},
    ['E'-32]={0x7f,0x49,0x49,0x49,0x41}, ['F'-32]={0x7f,9,9,9,1}, ['G'-32]={0x3e,0x41,0x49,0x49,0x7a}, ['H'-32]={0x7f,8,8,8,0x7f},
    ['I'-32]={0,0x41,0x7f,0x41,0}, ['J'-32]={0x20,0x40,0x41,0x3f,1}, ['K'-32]={0x7f,8,0x14,0x22,0x41}, ['L'-32]={0x7f,0x40,0x40,0x40,0x40},
    ['M'-32]={0x7f,2,0x0c,2,0x7f}, ['N'-32]={0x7f,4,8,0x10,0x7f}, ['O'-32]={0x3e,0x41,0x41,0x41,0x3e}, ['P'-32]={0x7f,9,9,9,6},
    ['Q'-32]={0x3e,0x41,0x51,0x21,0x5e}, ['R'-32]={0x7f,9,0x19,0x29,0x46}, ['S'-32]={0x46,0x49,0x49,0x49,0x31}, ['T'-32]={1,1,0x7f,1,1},
    ['U'-32]={0x3f,0x40,0x40,0x40,0x3f}, ['V'-32]={0x1f,0x20,0x40,0x20,0x1f}, ['W'-32]={0x3f,0x40,0x38,0x40,0x3f}, ['X'-32]={0x63,0x14,8,0x14,0x63},
    ['Y'-32]={7,8,0x70,8,7}, ['Z'-32]={0x61,0x51,0x49,0x45,0x43},
};

int esp32_display_text(const char *text, int x, int y, int fg, int bg, int scale)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    wait_for_transfer();
    if (!text || scale < 1 || scale > 4) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(text);
    if (len == 0 || x >= s_width || y >= s_height || y + 7 * scale <= 0) return ESP_OK;
    int max_chars = (s_width - (x > 0 ? x : 0) + 6 * scale - 1) / (6 * scale);
    if ((int)len > max_chars) len = (size_t)max_chars;
    int width = (int)len * 6 * scale;
    if (x < 0) { int skip = (-x + 6 * scale - 1) / (6 * scale); if ((size_t)skip >= len) return ESP_OK; text += skip; len -= (size_t)skip; x += skip * 6 * scale; width = (int)len * 6 * scale; }
    if (x + width > s_width) width = s_width - x;

    for (int gy = 0; gy < 7; gy++) {
        int col = 0;
        for (size_t ci = 0; ci < len && col < width; ci++) {
            unsigned char ch = (unsigned char)text[ci];
            ch = (unsigned char)toupper(ch);
            const uint8_t *glyph = (ch >= 32 && ch <= 90) ? font[ch - 32] : font['?'-32];
            for (int gx = 0; gx < 6 && col < width; gx++) {
                bool on = gx < 5 && ((glyph[gx] >> gy) & 1);
                for (int sx = 0; sx < scale && col < width; sx++) s_row[col++] = (uint16_t)(on ? fg : bg);
            }
        }
        while (col < width) s_row[col++] = (uint16_t)bg;
        for (int sy = 0; sy < scale; sy++) {
            int py = y + gy * scale + sy;
            if (py < 0 || py >= s_height) continue;
            esp_err_t err = draw_row(x, py, width);
            if (err != ESP_OK) return err;
        }
    }
    return ESP_OK;
}
