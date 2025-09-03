/*
 * TDK InvenSense ICM-20498
 * 6 DOF Gyro+Accel plus 3-axis mag
 *
 */

#include "hardware/icm_20948.h"

#include "dispatch/message_types.h"

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

void ICM20948::update() {
  icm_20948_DMP_data_t data;
  IMU_.readDMPdataFromFIFO(&data);  // TODO: consider rate-limiting this operation

  if ((IMU_.status == ICM_20948_Stat_Ok) ||
      (IMU_.status == ICM_20948_Stat_FIFOMoreDataAvail))  // Was valid data available?
  {
    MotionUpdate update(millis());
    if ((data.header & DMP_header_bitmap_Quat9) > 0) {
      // Scale to +/- 1
      update.qx = ((double)data.Quat9.Data.Q1) / 1073741824.0;  // Convert to double. Divide by 2^30
      update.qy = ((double)data.Quat9.Data.Q2) / 1073741824.0;  // Convert to double. Divide by 2^30
      update.qz = ((double)data.Quat9.Data.Q3) / 1073741824.0;  // Convert to double. Divide by 2^30
      update.hasOrientation = true;
    }
    if ((data.header & DMP_header_bitmap_Accel) > 0) {
      // Scale to Gs
      update.ax = ((double)data.Raw_Accel.Data.X) / 8192.0;
      update.ay = ((double)data.Raw_Accel.Data.Y) / 8192.0;
      update.az = ((double)data.Raw_Accel.Data.Z) / 8192.0;
      update.hasAcceleration = true;
    }
    etl::imessage_bus* bus = bus_;
    if ((update.hasOrientation || update.hasAcceleration) && bus) {
      // Invalidate insane orientation data
      if (update.hasOrientation) {
        if (isnan(update.qx) || isinf(update.qx) || update.qx < -1.1 || update.qx > 1.1 ||
            isnan(update.qy) || isinf(update.qy) || update.qy < -1.1 || update.qy > 1.1 ||
            isnan(update.qz) || isinf(update.qz) || update.qz < -1.1 || update.qz > 1.1) {
          char msg[100];
          snprintf(msg, sizeof(msg),
                   "ICM20948 invalid orientation Q1=%X; Q2=%X; Q3=%X (%.2f, %.2f, %.2f)",
                   data.Quat9.Data.Q1, data.Quat9.Data.Q2, data.Quat9.Data.Q3, update.qx, update.qy,
                   update.qz);
          Serial.println(msg);
          bus->receive(CommentMessage(msg));
          update.hasOrientation = false;
        }
      }

      // Invalidate insane acceleration data
      if (update.hasAcceleration) {
        if (isnan(update.ax) || isinf(update.ax) || update.ax < -1000 || update.ax > 1000 ||
            isnan(update.ay) || isinf(update.ay) || update.ay < -1000 || update.ay > 1000 ||
            isnan(update.az) || isinf(update.az) || update.az < -1000 || update.az > 1000) {
          char msg[100];
          snprintf(msg, sizeof(msg),
                   "ICM20948 invalid acceleration X=%X; Y=%X; Z=%X (%.2f, %.2f, %.2f)",
                   data.Raw_Accel.Data.X, data.Raw_Accel.Data.Y, data.Raw_Accel.Data.Z, update.ax,
                   update.ay, update.az);
          Serial.println(msg);
          bus->receive(CommentMessage(msg));
          update.hasAcceleration = false;
        }
      }

      // Only dispatch to bus if sanity checks pass
      if (update.hasOrientation || update.hasAcceleration) {
        bus->receive(update);
      }
    }
  }
}
