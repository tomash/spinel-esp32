# app/battery.rb -- M3 demo 2: ADC input. Reads the 16340 battery voltage
# from Ruby.
#
# T-OI Plus v1.2 battery sense: ADC1_CH0 on GPIO2, divider 2:1 (100k/100k),
# so full-scale at 3.3V VREF corresponds to ~6.6V at the cell. The ESP32-C3
# has no FPU; spinel lowers Float to double in software, fine for voltage
# math at this rate.
#
# esp_adc oneshot API (IDF v5.5): adc_oneshot_new_unit -> config channel
# -> adc_oneshot_read -> delete; adc_cali turns raw into calibrated mV
# using the per-chip eFuse curve. LilyGO T-OI Plus issue #19 measured the
# naive raw*3300/4095 mapping ~15% high on C3 (2954 raw vs 2060mV real at
# the pin) -- the eFuse curve-fitting scheme is the supported fix.
# Adapters wrap each step; Ruby does the formatting and divider math.

module ESP32
  ffi_source <<~C
    #include "esp_adc/adc_oneshot.h"
    #include "esp_adc/adc_cali.h"

      static adc_oneshot_unit_handle_t sp_adc_unit = NULL;
      static adc_cali_handle_t sp_adc_cali = NULL;
    int esp32_adc_init(int pin) {
        /* GPIO2 = ADC1_CH0 on ESP32-C3 */
        adc_oneshot_unit_init_cfg_t unit = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t e = adc_oneshot_new_unit(&unit, &sp_adc_unit);
        if (e != ESP_OK) return (int)e;
        adc_oneshot_chan_cfg_t chan = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        e = adc_oneshot_config_channel(sp_adc_unit, pin == 2 ? ADC_CHANNEL_0 : ADC_CHANNEL_1, &chan);
        if (e != ESP_OK) return (int)e;
        /* Curves are per-atten; 12 dB gives ~0-2500mV usable. Calibration
           is optional on C3 -- fall back to raw if creation fails. */
        adc_cali_curve_fitting_config_t cfg = {
            .unit_id = ADC_UNIT_1,
            .chan = pin == 2 ? ADC_CHANNEL_0 : ADC_CHANNEL_1,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        sp_adc_cali = NULL;
        e = adc_cali_create_scheme_curve_fitting(&cfg, &sp_adc_cali);
        return (e == ESP_OK) ? 0 : (int)e;
    }

    /* Calibrated pin voltage in mV, or negative if cali unavailable. */
    int esp32_adc_read_mv(void) {
        int raw = -1, mv = -1;
        if (sp_adc_unit == NULL) return -1;
        if (sp_adc_cali == NULL) return -2;
        esp_err_t e = adc_oneshot_read(sp_adc_unit, ADC_CHANNEL_0, &raw);
        if (e != ESP_OK) return -(int)e;
        e = adc_cali_raw_to_voltage(sp_adc_cali, raw, &mv);
        return (e == ESP_OK) ? mv : -(int)e;
    }

    /* Raw 12-bit code, for diagnostics when cali is unavailable. */
    int esp32_adc_read_raw(void) {
        int raw = -1;
        if (sp_adc_unit == NULL) return -1;
        esp_err_t e = adc_oneshot_read(sp_adc_unit, ADC_CHANNEL_0, &raw);
        return (e == ESP_OK) ? raw : -(int)e;
    }

    int esp32_adc_deinit(void) {
        if (sp_adc_cali) {
            adc_cali_delete_scheme_curve_fitting(sp_adc_cali);
            sp_adc_cali = NULL;
        }
        if (sp_adc_unit) {
            adc_oneshot_del_unit(sp_adc_unit);
            sp_adc_unit = NULL;
        }
        return 0;
    }
  C

  ffi_func :esp32_adc_init,      [:int], :int
  ffi_func :esp32_adc_read_mv,  [], :int
  ffi_func :esp32_adc_read_raw, [], :int
  ffi_func :esp32_adc_deinit,   [], :int
end

DIVIDER = 2 # 100k/100k divider: cell = 2x pin voltage

ESP32.esp32_adc_init(2)
puts "Reading battery on GPIO2 (ADC1_CH0, divider 2:1, eFuse-calibrated):"
count = 0
while count < 10
  mv = ESP32.esp32_adc_read_mv()
  if mv >= 0
    cell_mv = mv * DIVIDER
    volts = (cell_mv / 1000.0).round(2)
    puts "sample #{count}: pin=#{(mv / 1000.0).round(2)}V cell=#{volts}V"
  else
    raw = ESP32.esp32_adc_read_raw()
    if raw >= 0
      puts "sample #{count}: eFuse cali unavailable (code #{mv}); raw=#{raw} (uncalibrated)"
    else
      puts "sample #{count}: read failed (code #{-raw})"
    end
  end
  sleep 0.5
  count += 1
end
ESP32.esp32_adc_deinit()
puts "Battery demo done."
