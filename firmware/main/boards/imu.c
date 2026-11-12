/* boards/imu.c -- the two IMUs on the boards here, behind one interface:
 * accelerometer in milli-g and gyroscope in milli-degrees/second, +-8 g and
 * +-2000 dps full scale on both.
 *
 *  - MPU6886 (M5StickC), 0x68: big-endian registers 0x3B.. (accel, temp,
 *    gyro); 4096 LSB/g, 16.4 LSB/dps.
 *  - BMI270 (M5StickS3), 0x68: needs its 8 KB configuration file uploaded
 *    after reset (bmi270_config.h), as Bosch's API and M5Unified do; then
 *    little-endian accel at 0x0C and gyro at 0x12, same scales.
 */
#include "imu.h"
#include "board_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bmi270_config.h"
#include "esp_log.h"

static int s_kind, s_dev = -1;
static int s_acc[3], s_gyr[3];

static int mpu6886_init(void) {
    uint8_t who;
    if (bc_i2c_dev_read(s_dev, 0x75, &who, 1) != 0 || who != 0x19) return -2;
    bc_i2c_dev_write8(s_dev, 0x6B, 0x00); vTaskDelay(pdMS_TO_TICKS(10));
    bc_i2c_dev_write8(s_dev, 0x6B, 0x80); vTaskDelay(pdMS_TO_TICKS(10));   /* reset */
    bc_i2c_dev_write8(s_dev, 0x6B, 0x01); vTaskDelay(pdMS_TO_TICKS(10));   /* PLL clock */
    bc_i2c_dev_write8(s_dev, 0x1C, 0x10);   /* ACCEL_CONFIG: +-8 g */
    bc_i2c_dev_write8(s_dev, 0x1B, 0x18);   /* GYRO_CONFIG: +-2000 dps */
    bc_i2c_dev_write8(s_dev, 0x1A, 0x01);   /* CONFIG: DLPF */
    bc_i2c_dev_write8(s_dev, 0x19, 0x05);   /* SMPLRT_DIV */
    return 0;
}

/* In advanced power save (the state after reset) the BMI270 NACKs accesses
   closer than ~450 us apart, so every step retries with a tick between. */
static int bmi_w(uint8_t reg, const uint8_t *buf, int len) {
    for (int i = 0; i < 5; i++) {
        if (bc_i2c_dev_write(s_dev, reg, buf, len) == 0) return 0;
        vTaskDelay(1);
    }
    return -1;
}
static int bmi_w8(uint8_t reg, uint8_t v) { return bmi_w(reg, &v, 1); }

static int bmi270_init(void) {
    uint8_t id, st = 0, pc = 0;
    int retries = 0;
    if (bc_i2c_dev_read(s_dev, 0x00, &id, 1) != 0 || id != 0x24) return -2;
    bc_i2c_dev_write8(s_dev, 0x7E, 0xB6);           /* soft reset */
    vTaskDelay(pdMS_TO_TICKS(2) + 1);
    for (int i = 0; i < 16 && pc == 0; i++) {       /* ready when PWR_CONF reads back */
        vTaskDelay(1);
        bc_i2c_dev_read(s_dev, 0x7C, &pc, 1);
    }
    if (bmi_w8(0x7C, 0x00) != 0) return -6;         /* PWR_CONF: no advanced power save */
    vTaskDelay(1);
    bc_i2c_dev_read(s_dev, 0x7C, &pc, 1);
    if (pc != 0x00) ESP_LOGW("imu", "BMI270 PWR_CONF still 0x%02x", pc);
    if (bmi_w8(0x59, 0x00) != 0) return -7;         /* INIT_CTRL: start upload */
    /* the whole file in one burst from address 0, as M5Unified does */
    const uint8_t a0[2] = { 0, 0 };
    if (bmi_w(0x5B, a0, 2) != 0) return -3;                              /* INIT_ADDR_0/1 */
    if (bc_i2c_dev_write(s_dev, 0x5E, bmi270_config_file, sizeof bmi270_config_file) != 0) {
        retries++;
        return -4;
    }
    /* read the first bytes back through the same window to see they landed */
    uint8_t back[8] = {0};
    bmi_w(0x5B, a0, 2);
    bc_i2c_dev_read(s_dev, 0x5E, back, sizeof back);
    ESP_LOGI("imu", "BMI270 INIT_DATA readback %02x %02x %02x %02x (want %02x %02x %02x %02x)",
             back[0], back[1], back[2], back[3], bmi270_config_file[0], bmi270_config_file[1],
             bmi270_config_file[2], bmi270_config_file[3]);
    bmi_w8(0x59, 0x01);                             /* INIT_CTRL: done */
    bmi_w8(0x58, 0xFF);                             /* INT_MAP_DATA, as M5Unified */
    for (int i = 0; i < 100 && !(st & 0x0F); i++) { /* INTERNAL_STATUS: wait for a verdict */
        vTaskDelay(pdMS_TO_TICKS(5));
        bc_i2c_dev_read(s_dev, 0x21, &st, 1);
    }
    ESP_LOGI("imu", "BMI270 config upload: %d retried chunks, INTERNAL_STATUS 0x%02x", retries, st);
    if ((st & 0x0F) != 0x01) return -5;
    bc_i2c_dev_write8(s_dev, 0x7D, 0x0E);           /* PWR_CTRL: temp + acc + gyr */
    bc_i2c_dev_write8(s_dev, 0x40, 0xA8);           /* ACC_CONF: 100 Hz, normal */
    bc_i2c_dev_write8(s_dev, 0x41, 0x02);           /* ACC_RANGE: +-8 g */
    bc_i2c_dev_write8(s_dev, 0x42, 0xA9);           /* GYR_CONF: 200 Hz */
    bc_i2c_dev_write8(s_dev, 0x43, 0x00);           /* GYR_RANGE: +-2000 dps */
    bc_i2c_dev_write8(s_dev, 0x7C, 0x02);           /* PWR_CONF */
    return 0;
}

int imu_init(int kind, uint8_t addr) {
    if (s_kind) return 0;
    /* 100 kHz: the bus is the PMIC's (StickS3), with weak pull-ups for the
       8 KB BMI270 configuration burst at 400 kHz */
    s_dev = bc_i2c_add(addr, 100000);
    if (s_dev < 0) return -1;
    int rc = kind == IMU_MPU6886 ? mpu6886_init() : kind == IMU_BMI270 ? bmi270_init() : -1;
    if (rc == 0) s_kind = kind;
    return rc;
}

const char *imu_name(void) {
    return s_kind == IMU_MPU6886 ? "MPU6886" : s_kind == IMU_BMI270 ? "BMI270" : "none";
}

int imu_update(void) {
    uint8_t b[14];
    if (s_kind == IMU_MPU6886) {
        if (bc_i2c_dev_read(s_dev, 0x3B, b, 14) != 0) return -1;
        for (int i = 0; i < 3; i++) {
            int16_t a = (int16_t)(b[i * 2] << 8 | b[i * 2 + 1]);
            int16_t g = (int16_t)(b[8 + i * 2] << 8 | b[9 + i * 2]);
            s_acc[i] = a * 1000 / 4096;
            s_gyr[i] = (int)(g * 1000 / 16.4f);
        }
        return 0;
    }
    if (s_kind == IMU_BMI270) {
        if (bc_i2c_dev_read(s_dev, 0x0C, b, 12) != 0) return -1;
        for (int i = 0; i < 3; i++) {
            int16_t a = (int16_t)(b[i * 2] | b[i * 2 + 1] << 8);
            int16_t g = (int16_t)(b[6 + i * 2] | b[7 + i * 2] << 8);
            s_acc[i] = a * 1000 / 4096;
            s_gyr[i] = (int)(g * 1000 / 16.4f);
        }
        return 0;
    }
    return -1;
}

int imu_accel_mg(int axis) { return (s_kind && axis >= 0 && axis < 3) ? s_acc[axis] : 0; }
int imu_gyro_mdps(int axis) { return (s_kind && axis >= 0 && axis < 3) ? s_gyr[axis] : 0; }
