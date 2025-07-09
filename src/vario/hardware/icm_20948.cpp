/*
 * TDK InvenSense ICM-20498
 * 6 DOF Gyro+Accel plus 3-axis mag
 *
 */

#include "hardware/icm_20948.h"

#include "hardware/motion_source.h"

#define DEBUG_IMU 0

#define WIRE_PORT Wire
#define SERIAL_PORT Serial
#define AD0_VAL 0  // I2C address bit

void ICM20948::init() {
  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);

  if (DEBUG_IMU) IMU_.enableDebugging();  // enable helpful debug messages on Serial

  // TODO: Abort initialization attempt after some amount of time and show a fatal error
  bool initialized = false;
  while (!initialized) {
    IMU_.begin(WIRE_PORT, AD0_VAL);
    SERIAL_PORT.print(F("Initialization of the IMU returned: "));
    SERIAL_PORT.println(IMU_.statusString());
    if (IMU_.status != ICM_20948_Stat_Ok) {
      SERIAL_PORT.println("Trying again...");
      delay(500);
    } else {
      initialized = true;
    }
  }

  // TODO: Trigger a fatal error if configuration fails
  bool success = true;  // Use success to show if the DMP configuration was successful

  // Initialize the DMP. initializeDMP is a weak function. You can overwrite it if you want to e.g.
  // to change the sample rate
  success &= (IMU_.initializeDMP() == ICM_20948_Stat_Ok);

  // Enable the DMP orientation and accelerometer sensors
  success &= (IMU_.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);
  success &= (IMU_.enableDMPSensor(INV_ICM20948_SENSOR_ACCELEROMETER) == ICM_20948_Stat_Ok);

  // Configuring DMP to output data at multiple ODRs:
  success &= (IMU_.setDMPODRrate(DMP_ODR_Reg_Quat9, 2) == ICM_20948_Stat_Ok);  // Set to the maximum
  success &= (IMU_.setDMPODRrate(DMP_ODR_Reg_Accel, 2) == ICM_20948_Stat_Ok);  // Set to the maximum

  // Enable the FIFO
  success &= (IMU_.enableFIFO() == ICM_20948_Stat_Ok);

  // Enable the DMP
  success &= (IMU_.enableDMP() == ICM_20948_Stat_Ok);

  // Reset DMP
  success &= (IMU_.resetDMP() == ICM_20948_Stat_Ok);

  // Reset FIFO
  success &= (IMU_.resetFIFO() == ICM_20948_Stat_Ok);

  // Check success
  if (success) {
    SERIAL_PORT.println(F("DMP enabled!"));
  } else {
    SERIAL_PORT.println(F("Enable DMP failed!"));
  }
}

MotionUpdateResult ICM20948::update() {
  icm_20948_DMP_data_t data;
  IMU_.readDMPdataFromFIFO(&data);  // TODO: consider rate-limiting this operation

  bool hasQuat = false;
  bool hasAccel = false;

  if ((IMU_.status == ICM_20948_Stat_Ok) ||
      (IMU_.status == ICM_20948_Stat_FIFOMoreDataAvail))  // Was valid data available?
  {
    if ((data.header & DMP_header_bitmap_Quat9) > 0) {
      tQuaternion_ = millis();

      // Scale to +/- 1
      qx_ = ((double)data.Quat9.Data.Q1) / 1073741824.0;  // Convert to double. Divide by 2^30
      qy_ = ((double)data.Quat9.Data.Q2) / 1073741824.0;  // Convert to double. Divide by 2^30
      qz_ = ((double)data.Quat9.Data.Q3) / 1073741824.0;  // Convert to double. Divide by 2^30
      hasQuat = true;
    }
    if ((data.header & DMP_header_bitmap_Accel) > 0) {
      tAcceleration_ = millis();

      // Scale to Gs
      ax_ = ((double)data.Raw_Accel.Data.X) / 8192.0;
      ay_ = ((double)data.Raw_Accel.Data.Y) / 8192.0;
      az_ = ((double)data.Raw_Accel.Data.Z) / 8192.0;
      hasAccel = true;
    }
  }
  if (hasQuat && hasAccel) {
    return MotionUpdateResult::AccelerationReady | MotionUpdateResult::QuaternionReady;
  } else if (hasQuat) {
    return MotionUpdateResult::QuaternionReady;
  } else if (hasAccel) {
    return MotionUpdateResult::AccelerationReady;
  } else {
    return MotionUpdateResult::NoChange;
  }
}

void ICM20948::getOrientation(unsigned long* t, double* qx, double* qy, double* qz) {
  *t = tQuaternion_;
  *qx = qx_;
  *qy = qy_;
  *qz = qz_;
}

void ICM20948::getAcceleration(unsigned long* t, double* ax, double* ay, double* az) {
  *t = tAcceleration_;
  *ax = ax_;
  *ay = ay_;
  *az = az_;
}
