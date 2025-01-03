/*
 * IMU.cpp
 *
 * TDK InvenSense ICM-20498
 * 6 DOF Gyro+Accel plus 3-axis mag
 *
 */
#include "IMU.h"

#include <ICM_20948.h>

#include "Leaf_I2C.h"
#include "SDcard.h"
#include "log.h"

#define DEBUG_IMU 0

#define SERIAL_PORT Serial
#define WIRE_PORT Wire
#define AD0_VAL 0  // I2C address bit

ICM_20948_I2C IMU;

void imu_init() {
  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);

  if (DEBUG_IMU) IMU.enableDebugging();  // enable helpful debug messages on Serial

  bool initialized = false;
  while (!initialized) {
    IMU.begin(WIRE_PORT, AD0_VAL);
    SERIAL_PORT.print(F("Initialization of the IMU returned: "));
    SERIAL_PORT.println(IMU.statusString());
    if (IMU.status != ICM_20948_Stat_Ok) {
      SERIAL_PORT.println("Trying again...");
      delay(500);
    } else {
      initialized = true;
    }
  }
}

float ax, ay, az, at;

void imu_update() {
  // TODO: fill this in with everything

  if (IMU.dataReady()) {
    IMU.getAGMT();
    ax = IMU.accX();
    ay = IMU.accY();
    az = IMU.accZ();

    at = sqrt(ax * ax + ay * ay + az * az) / 1000;
  }

  if (DEBUG_IMU) {
    Serial.print("tot: ");
    Serial.print(at);
    Serial.print("x: ");
    Serial.print(ax);
    Serial.print(" y: ");
    Serial.print(ay);
    Serial.print(" z: ");
    Serial.println(az);
  }

  if (logbook.dataFileStarted) {
    String accelName = "accel,";
    String accelEntry = accelName + String(at);
    SDcard_writeData(accelEntry);
  }
}

float IMU_getAccel() { return at; }

/*

  pinMode(IMU_INTERRUPT, INPUT);

  delay(100);
  spi_writeIMUByte(address_REG_BANK_SEL, select_bank_0);
  delay(20);
  spi_writeIMUByte(address_USER_CTRL, default_USER_CTRL);
  delay(20);
  spi_writeIMUByte(address_PWR_MGMT_1, default_PWR_MGMT_1);
  delay(20);
  spi_writeIMUByte(address_PWR_MGMT_2, default_PWR_MGMT_2);
  delay(20);

  Serial.print("who am I: ");
  Serial.println(spi_readIMUByte(address_WHO_AM_I));

  Serial.print("user control: ");
  Serial.println(spi_readIMUByte(address_USER_CTRL));

  Serial.print("Power Management 1: ");
  Serial.println(spi_readIMUByte(address_PWR_MGMT_1));

  Serial.print("Power Management 2: ");
  Serial.println(spi_readIMUByte(address_PWR_MGMT_2));

*/
