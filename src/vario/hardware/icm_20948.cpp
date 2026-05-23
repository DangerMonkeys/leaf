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

namespace {
  // The DMP runs at 225Hz. The task manager polls the IMU at 20Hz, so keep the FIFO
  // production rate close to the consumer rate and drain bursts when the main loop is delayed.
  constexpr int DMP_ODR_INTERVAL_20HZ = 10;  // 225 / (10 + 1) = 20.45Hz
  constexpr uint8_t MAX_DMP_PACKETS_PER_UPDATE = 8;

  bool invalid(double value, double min, double max) {
    return isnan(value) || isinf(value) || value < min || value > max;
  }

  void publishComment(etl::imessage_bus* bus, const char* msg) {
    Serial.println(msg);
    if (bus) {
      bus->receive(CommentMessage(msg));
    }
  }
}  // namespace

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

  // Configuring DMP to output data at multiple ODRs.
  success &= (IMU_.setDMPODRrate(DMP_ODR_Reg_Quat9, DMP_ODR_INTERVAL_20HZ) ==
              ICM_20948_Stat_Ok);
  success &= (IMU_.setDMPODRrate(DMP_ODR_Reg_Accel, DMP_ODR_INTERVAL_20HZ) ==
              ICM_20948_Stat_Ok);

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
  etl::imessage_bus* bus = bus_;
  MotionUpdate latest(millis());
  bool moreData = false;

  for (uint8_t packet = 0; packet < MAX_DMP_PACKETS_PER_UPDATE; ++packet) {
    icm_20948_DMP_data_t data;
    IMU_.readDMPdataFromFIFO(&data);

    if (IMU_.status == ICM_20948_Stat_FIFONoDataAvail) {
      moreData = false;
      break;
    }

    if (IMU_.status == ICM_20948_Stat_FIFOIncompleteData) {
      publishComment(bus, "ICM20948 FIFO incomplete packet; resetting FIFO");
      IMU_.resetFIFO();
      return;
    }

    if ((IMU_.status != ICM_20948_Stat_Ok) &&
        (IMU_.status != ICM_20948_Stat_FIFOMoreDataAvail)) {
      char msg[100];
      snprintf(msg, sizeof(msg), "ICM20948 DMP FIFO read failed: %s", IMU_.statusString());
      publishComment(bus, msg);
      return;
    }

    moreData = (IMU_.status == ICM_20948_Stat_FIFOMoreDataAvail);
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
    if (update.hasOrientation || update.hasAcceleration) {
      // Invalidate insane orientation data
      if (update.hasOrientation) {
        if (invalid(update.qx, -1.1, 1.1) || invalid(update.qy, -1.1, 1.1) ||
            invalid(update.qz, -1.1, 1.1)) {
          char msg[100];
          snprintf(msg, sizeof(msg),
                   "ICM20948 invalid orientation Q1=%X; Q2=%X; Q3=%X (%.2f, %.2f, %.2f)",
                   data.Quat9.Data.Q1, data.Quat9.Data.Q2, data.Quat9.Data.Q3, update.qx, update.qy,
                   update.qz);
          publishComment(bus, msg);
          update.hasOrientation = false;
        }
      }

      // Invalidate insane acceleration data
      if (update.hasAcceleration) {
        if (invalid(update.ax, -8, 8) || invalid(update.ay, -8, 8) ||
            invalid(update.az, -8, 8)) {
          char msg[100];
          snprintf(msg, sizeof(msg),
                   "ICM20948 invalid acceleration X=%X; Y=%X; Z=%X (%.2f, %.2f, %.2f)",
                   data.Raw_Accel.Data.X, data.Raw_Accel.Data.Y, data.Raw_Accel.Data.Z, update.ax,
                   update.ay, update.az);
          publishComment(bus, msg);
          update.hasAcceleration = false;
        }
      }

      // Keep only the newest valid values from the FIFO burst.
      if (update.hasOrientation) {
        latest.qx = update.qx;
        latest.qy = update.qy;
        latest.qz = update.qz;
        latest.hasOrientation = true;
        latest.t = update.t;
      }
      if (update.hasAcceleration) {
        latest.ax = update.ax;
        latest.ay = update.ay;
        latest.az = update.az;
        latest.hasAcceleration = true;
        latest.t = update.t;
      }
    }

    if (!moreData) {
      break;
    }
  }

  if (moreData) {
    publishComment(bus, "ICM20948 FIFO backlog exceeded drain limit; resetting FIFO");
    IMU_.resetFIFO();
  }

  if (latest.hasOrientation && latest.hasAcceleration && bus) {
    bus->receive(latest);
  }
}
