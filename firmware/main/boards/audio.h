/* boards/audio.h -- see audio.c. */
#pragma once
#include <stdint.h>
int es8311_attach(uint8_t addr);                  /* on the board's I2C bus */
int es8311_tone(int freq, int ms, int volume_pct);   /* path stays open */
int es8311_stop(void);
int es8311_mic_level(int ms);                     /* RMS 0..32767, or < 0 */
int pdm_mic_level(int clk, int din, int ms);
