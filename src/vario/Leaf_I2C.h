#ifndef Leaf_I2C_h
#define Leaf_I2C_h

#include <Wire.h>

// these are default pins so we shouldn't need to use them
#define SDA_pin 8
#define SCL_pin 9

extern TwoWire Wire;

/* Three I2C devices are present on Leaf
 Address  -  Device
                0x77  -  Barometric Pressure Sensor MS5611-01BA03
                0x68  -  IMU 9DOF ICM-20948
                0x38  -  Temp & Humidity Sensor
*/
#define ADDR_IMU 0x68

void wire_init(void);

#endif