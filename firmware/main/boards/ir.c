/* boards/ir.c -- infrared NEC send/receive on the RMT peripheral.
 *
 * NEC: 9 ms mark + 4.5 ms space, 32 bits LSB first (address 16 bits, then
 * command + its complement), each bit a 560 us mark followed by a 560 us
 * (0) or 1690 us (1) space, then a 560 us stop mark; a held key repeats as
 * 9 ms + 2.25 ms + 560 us. TX modulates a 38 kHz carrier; RX gets the
 * demodulated pulse train from the board's IR receiver module.
 *
 * Pins come from the board (board_ir_pins); board_ir_prepare is the board's
 * hook for what IR needs around it (M5StickS3: PMIC 5 V output on, speaker
 * amplifier off -- its supply noise blocks reception). */
#include "board.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

__attribute__((weak)) void board_ir_pins(int *tx, int *rx) { *tx = -1; *rx = -1; }
__attribute__((weak)) int board_ir_prepare(void) { return 0; }

static rmt_channel_handle_t s_tx, s_rx;
static rmt_encoder_handle_t s_copy;
static QueueHandle_t s_rxq;
static rmt_symbol_word_t s_rxbuf[64];
static int s_addr = -1, s_cmd = -1;
static const rmt_receive_config_t s_rxcfg = {
    .signal_range_min_ns = 1250,          /* shorter is a glitch */
    .signal_range_max_ns = 12000000,      /* a gap longer than the leader ends a frame */
};

int board_ir_init(void) {
    int tx, rx;
    board_ir_pins(&tx, &rx);
    if (tx < 0 && rx < 0) return -1;
    board_ir_prepare();
    if (tx >= 0 && !s_tx) {
        rmt_tx_channel_config_t c = { .gpio_num = tx, .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000, .mem_block_symbols = 64, .trans_queue_depth = 4 };
        if (rmt_new_tx_channel(&c, &s_tx) != ESP_OK) return -2;
        rmt_carrier_config_t cc = { .frequency_hz = 38000, .duty_cycle = 0.33 };
        rmt_apply_carrier(s_tx, &cc);
        rmt_copy_encoder_config_t ec = {};
        rmt_new_copy_encoder(&ec, &s_copy);
        rmt_enable(s_tx);
    }
    if (rx >= 0 && !s_rx) {
        rmt_rx_channel_config_t c = { .gpio_num = rx, .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000, .mem_block_symbols = 64 };
        if (rmt_new_rx_channel(&c, &s_rx) != ESP_OK) return -3;
        s_rxq = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
        rmt_rx_event_callbacks_t cb = { .on_recv_done = NULL };
        extern bool ir_rx_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t *, void *);
        cb.on_recv_done = ir_rx_done;
        rmt_rx_register_event_callbacks(s_rx, &cb, s_rxq);
        rmt_enable(s_rx);
        rmt_receive(s_rx, s_rxbuf, sizeof s_rxbuf, &s_rxcfg);
    }
    return 0;
}

bool ir_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *ev, void *ctx) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)ctx, ev, &woken);
    return woken == pdTRUE;
}

int board_ir_send_nec(int addr, int cmd) {
    if (!s_tx) return -1;
    rmt_symbol_word_t f[34];
    int n = 0;
    f[n++] = (rmt_symbol_word_t){ .level0 = 1, .duration0 = 9000, .level1 = 0, .duration1 = 4500 };
    uint32_t bits = (uint32_t)(addr & 0xFFFF) | ((uint32_t)(cmd & 0xFF) << 16) | ((uint32_t)(~cmd & 0xFF) << 24);
    for (int i = 0; i < 32; i++)
        f[n++] = (rmt_symbol_word_t){ .level0 = 1, .duration0 = 560, .level1 = 0,
                                      .duration1 = (bits >> i) & 1 ? 1690 : 560 };
    f[n++] = (rmt_symbol_word_t){ .level0 = 1, .duration0 = 560, .level1 = 0, .duration1 = 0 };
    rmt_transmit_config_t tc = { .loop_count = 0 };
    if (rmt_transmit(s_tx, s_copy, f, sizeof f, &tc) != ESP_OK) return -2;
    return rmt_tx_wait_all_done(s_tx, 200) == ESP_OK ? 0 : -3;
}

/* IR receiver modules stretch marks and shorten spaces (most on the 9 ms
   leader), so the margin is a quarter of the expected length, 200 us at least */
static int near(uint32_t d, uint32_t want) {
    uint32_t m = want / 4 > 200 ? want / 4 : 200;
    return d + m > want && d < want + m;
}

/* Wait up to ms for a frame: 1 = NEC frame (board_ir_addr/cmd), 2 = repeat
   code, 0 = nothing, -1 = something that is not NEC, negative if no RX. */
int board_ir_poll(int ms) {
    if (!s_rx) return -2;
    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(s_rxq, &ev, pdMS_TO_TICKS(ms)) != pdTRUE) return 0;
    int rc = -1;
    rmt_symbol_word_t *s = ev.received_symbols;
    size_t n = ev.num_symbols;
    if (n < 2) {                          /* a lone blip is noise, not a frame */
        rmt_receive(s_rx, s_rxbuf, sizeof s_rxbuf, &s_rxcfg);
        return 0;
    }
    /* skip noise ahead of the frame: the leader may come a few symbols in */
    for (int k = 0; k < 3 && n > 2; k++) {
        if (near(s[0].duration0, 9000)) break;
        s++; n--;
    }
    /* the receiver module is active-low: a carrier burst reads as level 0 */
    if (n >= 33 && near(s[0].duration0, 9000) && near(s[0].duration1, 4500)) {
        uint32_t bits = 0;
        int ok = 1;
        for (int i = 0; i < 32 && ok; i++) {
            if (!near(s[1 + i].duration0, 560)) ok = 0;
            else if (near(s[1 + i].duration1, 1690)) bits |= 1u << i;
            else if (!near(s[1 + i].duration1, 560)) ok = 0;
        }
        if (ok) { s_addr = bits & 0xFFFF; s_cmd = (bits >> 16) & 0xFF; rc = 1; }
    } else if (n >= 2 && near(s[0].duration0, 9000) && near(s[0].duration1, 2250)) {
        rc = 2;
    }
    if (rc == -1) {
        ESP_LOGI("ir", "not NEC: %u symbols; first %u/%u %u/%u %u/%u %u/%u", (unsigned)n,
                 n > 0 ? s[0].level0 : 0, n > 0 ? s[0].duration0 : 0,
                 n > 0 ? s[0].level1 : 0, n > 0 ? s[0].duration1 : 0,
                 n > 1 ? s[1].duration0 : 0, n > 1 ? s[1].duration1 : 0,
                 n > 2 ? s[2].duration0 : 0, n > 2 ? s[2].duration1 : 0);
    }
    rmt_receive(s_rx, s_rxbuf, sizeof s_rxbuf, &s_rxcfg);
    return rc;
}
int board_ir_addr(void) { return s_addr; }
int board_ir_cmd(void) { return s_cmd; }
