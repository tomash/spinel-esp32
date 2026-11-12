# app/imu.rb -- the board's IMU from Ruby: acceleration, rotation, tilt.
#
# M5StickC (MPU6886) and M5StickS3 (BMI270) through board_imu_*. Prints a
# sample twice a second -- acceleration in g, rotation in deg/s, and pitch /
# roll from the gravity vector -- and shows it on the display when there is
# one. Lie the board flat: z reads about 1 g; tip it and pitch/roll follow.

module Board
  ffi_func :board_name,          [], :str
  ffi_func :board_imu_init,      [], :int
  ffi_func :board_imu_name,      [], :str
  ffi_func :board_imu_update,    [], :int
  ffi_func :board_imu_accel_mg,  [:int], :int
  ffi_func :board_imu_gyro_mdps, [:int], :int
  ffi_func :board_has_lcd,       [], :int
  ffi_func :esp32_display_init,  [], :int
  ffi_func :esp32_display_set_orientation, [:int], :int
  ffi_func :esp32_display_clear, [:int], :int
  ffi_func :esp32_display_text,  [:str, :int, :int, :int, :int, :int], :int
  ffi_source <<~C
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    int imu_delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS((uint32_t)ms)); return 0; }
  C
  ffi_func :imu_delay_ms, [:int], :int, blocking: true
end

def g(mg)
  (mg / 1000.0).round(2)
end

puts "board: #{Board.board_name}"
rc = Board.board_imu_init
if rc != 0
  puts "imu: none (#{rc})"
else
  puts "imu: #{Board.board_imu_name}"
  lcd = Board.board_has_lcd == 1 && Board.esp32_display_init == 0
  if lcd
    Board.esp32_display_set_orientation(1)
    Board.esp32_display_clear(0)
  end
  while true
    if Board.board_imu_update == 0
      ax = Board.board_imu_accel_mg(0)
      ay = Board.board_imu_accel_mg(1)
      az = Board.board_imu_accel_mg(2)
      gx = Board.board_imu_gyro_mdps(0) / 1000
      gy = Board.board_imu_gyro_mdps(1) / 1000
      gz = Board.board_imu_gyro_mdps(2) / 1000
      pitch = (Math.atan2(-ax, Math.sqrt(ay * ay + az * az)) * 180 / Math::PI).round
      roll  = (Math.atan2(ay, az) * 180 / Math::PI).round
      puts "acc g #{g(ax)} #{g(ay)} #{g(az)}  gyro dps #{gx} #{gy} #{gz}  pitch #{pitch} roll #{roll}"
      if lcd
        Board.esp32_display_text("#{Board.board_imu_name}            ", 2, 2, 0xFFFF, 0, 1)
        Board.esp32_display_text("ACC #{g(ax)} #{g(ay)} #{g(az)}      ", 2, 16, 0x07E0, 0, 1)
        Board.esp32_display_text("GYR #{gx} #{gy} #{gz}        ", 2, 30, 0xFFE0, 0, 1)
        Board.esp32_display_text("PITCH #{pitch}  ROLL #{roll}      ", 2, 44, 0x07FF, 0, 1)
      end
    end
    Board.imu_delay_ms(500)
  end
end
