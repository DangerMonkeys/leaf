#ifndef Leaf_I2C_h
#define Leaf_I2C_h

#include <Wire.h>

// these are default pins so we shouldn't need to use them
#define SDA_pin 8
#define SCL_pin 9

extern TwoWire Wire;

/* I2C devices (variant dependent)
 Address  -  Device
                0x77  -  Barometric Pressure Sensor MS5611-01BA03
                0x68  -  IMU 9DOF ICM-20948
                0x6A  -  LSM6DSV on v3.2.8 (replaces ICM-20948)
                0x30  -  MMC5983MA on v3.2.8 (unused)
                0x38  -  Temp & Humidity Sensor
*/
#define ADDR_IMU 0x68

void wire_init(void);

#endif
