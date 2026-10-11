# app/i2c.rb -- M3 demo 3: I2C bus scan from Ruby.
#
# T-OI Plus v1.2 Grove connector: SDA=GPIO18, SCL=GPIO19, with onboard
# (or internal) pull-ups. Demos the new IDF v5.x i2c master API through
# the FFI DSL: i2c_new_master_bus + i2c_master_probe over the 7-bit
# address space. Probing is the honest first contact: without a sensor
# attached this reports no devices; plug any Grove I2C module to see
# addresses appear.

module ESP32
  ffi_source <<~C
    #include "driver/i2c_master.h"

    static i2c_master_bus_handle_t sp_i2c_bus = NULL;

    int esp32_i2c_init(int sda, int scl) {
        if (sp_i2c_bus) return 0; /* already up */
        i2c_master_bus_config_t cfg = {
            .i2c_port = -1,          /* auto port */
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = { .enable_internal_pullup = 1 },
        };
        return (int)i2c_new_master_bus(&cfg, &sp_i2c_bus);
    }

    /* probe a 7-bit address; 0 = ACK (device present), 1 = no device */
    int esp32_i2c_probe(int addr) {
        esp_err_t e = i2c_master_probe(sp_i2c_bus, (uint16_t)addr, 50 /*ms*/);
        return (e == ESP_OK) ? 0 : 1;
    }

    int esp32_i2c_deinit(void) {
        if (sp_i2c_bus) { i2c_del_master_bus(sp_i2c_bus); sp_i2c_bus = NULL; }
        return 0;
    }
  C

  ffi_func :esp32_i2c_init,   [:int, :int], :int
  ffi_func :esp32_i2c_probe,  [:int], :int
  ffi_func :esp32_i2c_deinit, [], :int
end

SDA = 18
SCL = 19

rc = ESP32.esp32_i2c_init(SDA, SCL)
if rc != 0
  puts "i2c_init failed with code #{rc}"
else
  puts "I2C master bus up on SDA=#{SDA} SCL=#{SCL} (100 kHz default). Scanning..."
  found = []
  addr = 8
  while addr < 120
    if ESP32.esp32_i2c_probe(addr) == 0
      found.push(addr)
    end
    addr += 1
  end
  if found.empty?
    puts "No I2C devices answered on the bus."
  else
    puts "Found #{found.length} device(s):"
    found.each { |a| puts format("  0x%02X", a) }
  end
  ESP32.esp32_i2c_deinit()
  puts "I2C scan done."
end
