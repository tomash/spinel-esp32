# app/battery.rb -- M3 demo 2: ADC input. Reads the 16340 battery voltage
# from Ruby.
#
# T-OI Plus v1.2 battery sense: **ADC1_CH2** on GPIO2, divider 2:1 (100k/100k),
# so full-scale at 3.3V VREF corresponds to ~6.6V at the cell. The ESP32-C3
# has no FPU; spinel lowers Float to double in software, fine for voltage
# math at this rate.
#
# esp_adc oneshot API (IDF v5.5): adc_oneshot_new_unit -> config channel
# -> adc_oneshot_read -> delete; adc_cali turns raw into calibrated mV
# using the per-chip eFuse characterization. LilyGO T-OI Plus issue #19
# measured the naive raw*3300/4095 mapping ~15% high on C3 (2954 raw vs
# 2060mV real at the pin) -- eFuse calibration is the supported fix.
#
# The calibration *scheme* is per chip, chosen at compile time by the
# esp_adc scheme macros (never both are defined for a target):
#   esp32c3 -> ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED (eFuse curve)
#   esp32   -> ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED  (eFuse vref/TP line)
# Adapters wrap each step; Ruby does the formatting and divider math.

module ESP32
  ffi_source <<~C
    #include "esp_adc/adc_oneshot.h"
    /* Scheme-agnostic calibration API plus the per-chip
       ADC_CALI_SCHEME_{CURVE,LINE}_FITTING_SUPPORTED macros that select the
       scheme at compile time (this header also pulls adc_cali.h and the
       scheme structs + create/delete prototypes). */
    #include "esp_adc/adc_cali_scheme.h"

      static adc_oneshot_unit_handle_t sp_adc_unit = NULL;
      static adc_cali_handle_t sp_adc_cali = NULL;
      static adc_channel_t sp_adc_chan = ADC_CHANNEL_2;
    int esp32_adc_init(int pin) {
        /* GPIO2 = ADC1_CH2 on ESP32-C3 (CH0 is GPIO0). LilyGO's own
           battery_voltage example uses BAT_ADC pin 2, divider x2. */
        adc_oneshot_unit_init_cfg_t unit = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t e = adc_oneshot_new_unit(&unit, &sp_adc_unit);
        if (e != ESP_OK) return (int)e;
        sp_adc_chan = (pin == 2) ? ADC_CHANNEL_2 : ADC_CHANNEL_1;
        adc_oneshot_chan_cfg_t chan = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        e = adc_oneshot_config_channel(sp_adc_unit, sp_adc_chan, &chan);
        if (e != ESP_OK) return (int)e;
        /* Curves/characterization are per-atten; 12 dB gives ~0-2500mV
           usable. Calibration is optional -- return the code on failure
           and the demo falls back to raw readings. */
        sp_adc_cali = NULL;
    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cfg = {
            .unit_id = ADC_UNIT_1,
            .chan = sp_adc_chan,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        e = adc_cali_create_scheme_curve_fitting(&cfg, &sp_adc_cali);
    #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cfg = {
            .unit_id = ADC_UNIT_1,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
    #if CONFIG_IDF_TARGET_ESP32
            /* Consulted only when the eFuse holds no calibration value
               (ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF). Field exists
               on classic ESP32 only; C3 uses curve fitting and has no such
               member. IDF's default reference is 1100 mV. */
            .default_vref = 1100,
    #endif
        };
        e = adc_cali_create_scheme_line_fitting(&cfg, &sp_adc_cali);
    #else
    #error "no ADC calibration scheme supported for this target"
    #endif
        return (e == ESP_OK) ? 0 : (int)e;
    }

    /* Calibrated pin voltage in mV, or negative if cali unavailable. */
    int esp32_adc_read_mv(void) {
        int raw = -1, mv = -1;
        if (sp_adc_unit == NULL) return -1;
        if (sp_adc_cali == NULL) return -2;
        esp_err_t e = adc_oneshot_read(sp_adc_unit, sp_adc_chan, &raw);
        if (e != ESP_OK) return -(int)e;
        e = adc_cali_raw_to_voltage(sp_adc_cali, raw, &mv);
        return (e == ESP_OK) ? mv : -(int)e;
    }

    /* Raw 12-bit code, for diagnostics when cali is unavailable. */
    int esp32_adc_read_raw(void) {
        int raw = -1;
        if (sp_adc_unit == NULL) return -1;
        esp_err_t e = adc_oneshot_read(sp_adc_unit, sp_adc_chan, &raw);
        return (e == ESP_OK) ? raw : -(int)e;
    }

    int esp32_adc_deinit(void) {
        if (sp_adc_cali) {
    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            adc_cali_delete_scheme_curve_fitting(sp_adc_cali);
    #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            adc_cali_delete_scheme_line_fitting(sp_adc_cali);
    #endif
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
puts "Reading battery on GPIO2 (ADC1_CH2, divider 2:1, eFuse-calibrated):"
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
