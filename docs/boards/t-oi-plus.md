# LilyGO T-OI Plus (v1.2) — ESP32-C3 board profile

The M2/M3 hardware target. Also sold as "mini D1 plus". D1-mini form factor,
Grove connector, 16340 Li-ion holder with charging and battery voltage metering.

## Facts

| Item | Value |
|---|---|
| MCU | ESP32-C3 (RISC-V RV32IMAC, single core, 160 MHz) |
| Flash | 4 MB (QIO) |
| SRAM | 400 KB |
| Charger | TP4054 (no on-board protection — use protected 16340 cells) |
| USB serial | CH340 (Linux: no driver needed) |
| Boot/download | GPIO9 (hold BOOT while resetting) |

## Pin map (board pin → GPIO)

| Board pin | GPIO | Notes |
|---|---|---|
| A1 | 2  | **battery voltage via ADC1_CH2** (divider to 16340 holder); strapping pin |
| LED_BUILTIN | 3  | safest free GPIO — first blink target |
| SCK / A2 | 4 | JTAG TMS; strapping |
| MISO / A3 | 5 | JTAG TDI; strapping |
| MOSI | 6 | JTAG TCK; strapping |
| SS | 7 | JTAG TDO; strapping |
| SCL | 18 | I2C default SCL (Grove) |
| SDA | 19 | I2C default SDA (Grove) |
| RX | 20 | UART0 RXD (console) |
| TX | 21 | UART0 TXD (console) |

## M3 example programs this board enables

- `blink.rb` — GPIO3 LED (plain digital out).
- `battery.rb` — ADC on GPIO2, scaled through the divider, printed over UART.
  The classic first "real" FFI demo: `adc1_get_raw` via ffi_func.
- `i2c.rb` — Grove connector on GPIO18/19 (BME280 or similar sensor).

## References

- espboards.dev board page (pin map source): https://www.espboards.dev/esp32/ttgo-t-oi-plus/
- LilyGO product page: https://lilygo.cc/products/t-oi-plus
- Xinyuan-LilyGO/LilyGo-T-OI-PLUS (official repo): https://github.com/Xinyuan-LilyGO/LilyGo-T-OI-PLUS
