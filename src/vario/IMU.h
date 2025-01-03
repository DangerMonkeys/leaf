#ifndef IMU_h
#define IMU_h

#include <Arduino.h>

#define IMU_INTERRUPT 7  // 14 on V3.2.0  // INPUT

#define address_WHO_AM_I 0
#define address_USER_CTRL 3
#define address_PWR_MGMT_1 6
#define address_PWR_MGMT_2 7
#define address_REG_BANK_SEL 127

#define default_USER_CTRL 0b01010000  // reset I2C and put into SPI mode
#define default_PWR_MGMT_1 \
  0b00000100  // reset, wake up, low power off, temp on, clock '4' (auto select best source)
#define default_PWR_MGMT_2 0b00000000  // accel on, gyro on
#define select_bank_0 0b00000000       // bank 0
#define select_bank_1 0b00010000       // bank 1
#define select_bank_2 0b00100000       // bank 2
#define select_bank_3 0b00110000       // bank 3

void imu_init(void);
void imu_update(void);

float IMU_getAccel(void);

#endif
