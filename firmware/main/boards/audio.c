/* boards/audio.c -- speaker tone and microphone level.
 *
 *  - ES8311 codec (M5StickS3): I2C 0x18 on the PMIC's bus; I2S MCLK18,
 *    BCLK17, LRCK15, DOUT14 (to the codec's DAC), DIN16 (from its ADC). The
 *    codec derives its master clock from BCLK (reg 0x01, x8 pre-multiplier),
 *    so a plain 16-bit stereo stream is enough. Register sequences from
 *    M5Unified (_speaker_enabled_cb_sticks3 / _microphone_enabled_cb_sticks3).
 *    The speaker amplifier is the board's (PMIC GPIO3), switched by the board
 *    around a tone.
 *  - PDM microphone (M5StickC SPM1423): clock G0, data G34.
 *
 * One I2S user at a time: a tone or a level reading opens the channel it
 * needs and closes it again, so speaker, microphone and IR do not overlap. */
#include <math.h>
#include <stdlib.h>
#include "audio.h"
#include "board_common.h"
#include "soc/soc_caps.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static int s_codec = -1;

/* register/value pairs ending in 0xFF (not 0: register 0x00 comes first) */
#define ES_END 0xFF
static int es8311_seq(const uint8_t *seq) {
    int fails = 0;
    for (; seq[0] != ES_END; seq += 2)
        if (bc_i2c_dev_write8(s_codec, seq[0], seq[1]) != 0) {
            if (!fails) ESP_LOGW("audio", "ES8311 write reg 0x%02x failed", seq[0]);
            fails++;
        }
    return fails;
}

int es8311_attach(uint8_t addr) {
    if (s_codec >= 0) return 0;
    s_codec = bc_i2c_add(addr, 100000);
    if (s_codec < 0) return -1;
    /* one byte per read: the ES8311 does not auto-increment */
    uint8_t id[2] = { 0, 0 };
    int rc = bc_i2c_dev_read(s_codec, 0xFD, &id[0], 1) | bc_i2c_dev_read(s_codec, 0xFE, &id[1], 1);
    ESP_LOGI("audio", "ES8311 at 0x%02x: id read %s, %02x %02x (want 83 11)",
             addr, rc == 0 ? "ok" : "FAILED", id[0], id[1]);
    return 0;
}

static i2s_chan_handle_t std_chan(int port, int tx, int rate, int dout, int din) {
    i2s_chan_handle_t h = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    if (i2s_new_channel(&cc, tx ? &h : NULL, tx ? NULL : &h) != ESP_OK) return NULL;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = 18, .bclk = 17, .ws = 15, .dout = dout, .din = din },
    };
    if (i2s_channel_init_std_mode(h, &sc) != ESP_OK || i2s_channel_enable(h) != ESP_OK) {
        i2s_del_channel(h); return NULL;
    }
    return h;
}

/* The speaker path stays open across tones (codec set up, I2S running) so a
   melody plays without the click of tearing it down between notes;
   es8311_stop closes it. Volume scales the samples: the codec's DAC register
   stays at 0 dB (0xBF) as M5Unified has it -- that register steps 0.5 dB, so
   a linear percentage there is nearly silent. */
static i2s_chan_handle_t s_spk;
static const int SPK_RATE = 22050;

int es8311_tone(int freq, int ms, int volume_pct) {
    if (s_codec < 0) return -1;
    if (volume_pct < 0) volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    if (!s_spk) {
        s_spk = std_chan(I2S_NUM_0, 1, SPK_RATE, 14, I2S_GPIO_UNUSED);
        if (!s_spk) return -2;
        const uint8_t on[] = { 0x00,0x80, 0x01,0xB5, 0x02,0x18, 0x0D,0x01, 0x12,0x00,
                               0x13,0x10, 0x32,0xBF, 0x37,0x08, ES_END };
        int f = es8311_seq(on);
        if (f) ESP_LOGW("audio", "ES8311 speaker setup: %d failed writes", f);
    }
    int amp = 30000 * volume_pct / 100;
    int16_t buf[256 * 2];
    int total = SPK_RATE * ms / 1000, done = 0;
    double ph = 0, step = 2 * M_PI * freq / SPK_RATE;
    while (done < total) {
        int n = total - done < 256 ? total - done : 256;
        for (int i = 0; i < n; i++) {
            /* 5 ms fade in and out, so notes start and stop without a click */
            int k = done + i, edge = SPK_RATE / 200;
            double env = k < edge ? (double)k / edge : (total - k < edge ? (double)(total - k) / edge : 1.0);
            int16_t v = (int16_t)(sin(ph) * amp * env);
            buf[i * 2] = buf[i * 2 + 1] = v;
            ph += step;
        }
        size_t w;
        i2s_channel_write(s_spk, buf, n * 4, &w, portMAX_DELAY);
        done += n;
    }
    return 0;
}

int es8311_stop(void) {
    if (!s_spk) return 0;
    int16_t z[512] = {0};
    size_t w;
    i2s_channel_write(s_spk, z, sizeof z, &w, portMAX_DELAY);   /* drain with silence */
    i2s_channel_disable(s_spk); i2s_del_channel(s_spk);
    s_spk = NULL;
    return 0;
}

static int rms16(i2s_chan_handle_t h, int rate, int ms, int stride) {
    int16_t buf[512];
    int total = rate * ms / 1000 * stride, got = 0;
    double acc = 0, sum = 0; long n = 0;
    /* drop the first 100 ms: DC settling. The level is the AC part only
       (standard deviation): PDM mics carry a large DC offset. */
    int skip = rate / 10 * stride;
    while (got < total + skip) {
        size_t r = 0;
        if (i2s_channel_read(h, buf, sizeof buf, &r, 1000) != ESP_OK) break;
        int k = r / 2;
        for (int i = 0; i < k; i += stride) {
            if (got++ >= skip) { acc += (double)buf[i] * buf[i]; sum += buf[i]; n++; }
        }
    }
    if (!n) return -3;
    double mean = sum / n, var = acc / n - mean * mean;
    return var > 0 ? (int)sqrt(var) : 0;
}

int es8311_mic_level(int ms) {
    if (s_codec < 0) return -1;
    const uint8_t on[] = { 0x00,0x80, 0x01,0xBA, 0x02,0x18, 0x0D,0x01, 0x0E,0x02,
                           0x14,0x10, 0x17,0xFF, 0x1C,0x6A, ES_END };
    const uint8_t off[] = { 0x0D,0xFC, 0x0E,0x6A, 0x00,0x00, ES_END };
#if SOC_I2S_NUM > 1
    i2s_chan_handle_t h = std_chan(I2S_NUM_1, 0, 16000, I2S_GPIO_UNUSED, 16);
#else
    i2s_chan_handle_t h = NULL;           /* single-I2S chips: no codec mic */
#endif
    if (!h) return -2;
    es8311_seq(on);
    int lvl = rms16(h, 16000, ms, 2);     /* stereo slots: take the left */
    es8311_seq(off);
    i2s_channel_disable(h); i2s_del_channel(h);
    return lvl;
}

int pdm_mic_level(int clk, int din, int ms) {
    i2s_chan_handle_t h = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&cc, NULL, &h) != ESP_OK) return -2;
    i2s_pdm_rx_config_t pc = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = clk, .din = din },
    };
    if (i2s_channel_init_pdm_rx_mode(h, &pc) != ESP_OK || i2s_channel_enable(h) != ESP_OK) {
        i2s_del_channel(h); return -2;
    }
    int lvl = rms16(h, 16000, ms, 1);
    i2s_channel_disable(h); i2s_del_channel(h);
    return lvl;
}
