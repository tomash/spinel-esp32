/* boards/imu.h -- see imu.c. */
#pragma once
#include <stdint.h>
enum { IMU_NONE = 0, IMU_MPU6886 = 1, IMU_BMI270 = 2 };
int imu_init(int kind, uint8_t addr);   /* on the board's I2C bus (bc_i2c_init first) */
const char *imu_name(void);
int imu_update(void);
int imu_accel_mg(int axis);
int imu_gyro_mdps(int axis);
