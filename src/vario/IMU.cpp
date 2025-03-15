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
#include "kalmanvert/kalmanvert.h"
#include "log.h"
#include "telemetry.h"

// === What to display on the serial port
// #define ALIGN_TEXT 3  // When set, puts values in equal-width columns with this many digits
// #define SHOW_QUATERNION  // When set, print the components of the orientation quaternion
// #define SHOW_DEVICE_ACCEL  // When set, print the components of the device-frame acceleration
// #define SHOW_WORLD_ACCEL  // When set, print the components of the world-frame acceleration
// #define SHOW_VERTICAL_ACCEL  // When set, print the vertical acceleration (with gravity removed)
// #define SHOW_FIXED_BOUNDS 0.2  // When set, print fixed values to keep the Arduino serial plot in
// a consistent range

// Kalman filter object for vertical climb rate and position
Kalmanvert kalmanvert;

#define POSITION_MEASURE_STANDARD_DEVIATION 0.1f
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3f

#define DEBUG_IMU 0

#define SERIAL_PORT Serial
#define WIRE_PORT Wire
#define AD0_VAL 0  // I2C address bit

ICM_20948_I2C IMU;

// == Estimation of constant gravity acceleration ==
double zAvg = 1.0;                          // Best guess for strength of gravity
uint32_t tPrev;                             // Last time gravity guess was updated
const double NEW_MEASUREMENT_WEIGHT = 0.9;  // Weight given to new measurements...
const double AFTER_SECONDS = 5.0;           // ...after this number of seconds
const double K_UPDATE = log(1 - NEW_MEASUREMENT_WEIGHT) / AFTER_SECONDS;

void quaternionMult(double qw,
                    double qx,
                    double qy,
                    double qz,
                    double rw,
                    double rx,
                    double ry,
                    double rz,
                    double* pw,
                    double* px,
                    double* py,
                    double* pz) {
  *pw = rw * qw - rx * qx - ry * qy - rz * qz;
  *px = rw * qx + rx * qw - ry * qz + rz * qy;
  *py = rw * qy + rx * qz + ry * qw - rz * qx;
  *pz = rw * qz - rx * qy + ry * qx + rz * qw;
}

void rotateByQuaternion(double px,
                        double py,
                        double pz,
                        double qw,
                        double qx,
                        double qy,
                        double qz,
                        double* p1x,
                        double* p1y,
                        double* p1z) {
  double qrw, qrx, qry, qrz, qcw;
  quaternionMult(qw, qx, qy, qz, 0, px, py, pz, &qrw, &qrx, &qry, &qrz);
  quaternionMult(qrw, qrx, qry, qrz, qw, -qx, -qy, -qz, &qcw, p1x, p1y, p1z);
}

inline void printFloat(double v) {
#ifdef ALIGN_TEXT
  if (v >= 0) {
    Serial.print(' ');
  }
  const int digits = ALIGN_TEXT;
#else
  const int digits = 4;
#endif
  Serial.print(v, digits);
}

double accelVert;
double accelTot;

bool processQuaternion() {
  bool success = false;
  icm_20948_DMP_data_t data;
  IMU.readDMPdataFromFIFO(&data);

  if ((IMU.status == ICM_20948_Stat_Ok) ||
      (IMU.status == ICM_20948_Stat_FIFOMoreDataAvail))  // Was valid data available?
  {
    if ((data.header & DMP_header_bitmap_Quat9) > 0 &&
        (data.header & DMP_header_bitmap_Accel) > 0) {
      uint32_t t = millis();
      double dt = ((double)t - tPrev) * 0.001;
      double f = exp(K_UPDATE * dt);
      tPrev = t;

      // Scale to +/- 1
      double qx = ((double)data.Quat9.Data.Q1) / 1073741824.0;  // Convert to double. Divide by 2^30
      double qy = ((double)data.Quat9.Data.Q2) / 1073741824.0;  // Convert to double. Divide by 2^30
      double qz = ((double)data.Quat9.Data.Q3) / 1073741824.0;  // Convert to double. Divide by 2^30
      double magnitude = ((qx * qx) + (qy * qy) + (qz * qz));
      if (magnitude >= 1.0) magnitude = 1.0;
      double qw = sqrt(1.0 - magnitude);

      bool needComma = false;
      bool needNewline = false;
      
#ifdef SHOW_QUATERNION
      Serial.print(F("Qw:"));
      printFloat(qw);
      Serial.print(F(",Qx:"));
      printFloat(qx);
      Serial.print(F(",Qy:"));
      printFloat(qy);
      Serial.print(F(",Qz:"));
      printFloat(qz);
      needComma = true;
      needNewline = true;

#endif

      double ax = ((double)data.Raw_Accel.Data.X) / 8192.0;
      double ay = ((double)data.Raw_Accel.Data.Y) / 8192.0;
      double az = ((double)data.Raw_Accel.Data.Z) / 8192.0;

      accelTot = sqrt(ax * ax + ay * ay + az * az);

#ifdef SHOW_DEVICE_ACCEL
      if (needComma) {
        Serial.print(',');
      }
      Serial.print(F("Ax:"));
      printFloat(ax);
      Serial.print(F(",Ay:"));
      printFloat(ay);
      Serial.print(F(",Az:"));
      printFloat(az);
      needComma = true;
      needNewline = true;
#endif

      double awx, awy, awz;
      rotateByQuaternion(ax, ay, az, qw, qx, qy, qz, &awx, &awy, &awz);

      accelVert = awz - zAvg;

#ifdef SHOW_WORLD_ACCEL
      if (needComma) {
        Serial.print(',');
      }
      Serial.print("Wx:");
      printFloat(awx);
      Serial.print(",Wy:");
      printFloat(awy);
      Serial.print(",Wz:");
      printFloat(awz);
      needComma = true;
      needNewline = true;
#endif

#ifdef SHOW_VERTICAL_ACCEL
      if (needComma) {
        Serial.print(',');
      }
      Serial.print("dAz:");
      printFloat(accelVert);
      needComma = true;
      needNewline = true;
#endif

#ifdef SHOW_FIXED_BOUNDS
      if (needComma) {
        Serial.print(',');
      }
      Serial.print("min:");
      printFloat(SHOW_FIXED_BOUNDS);
      Serial.print(",max:");
      printFloat(-SHOW_FIXED_BOUNDS);
      needNewline = true;
#endif

      zAvg = zAvg * f + awz * (1 - f);

      if (needNewline) {
        Serial.println();
      }

      success = true;
    }
  }
  return success;
}

/*************************************************
 * Initialize IMU, Quaternions, and Kalman Filter *
 *************************************************/
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

  bool success = true;  // Use success to show if the DMP configuration was successful

  // Initialize the DMP. initializeDMP is a weak function. You can overwrite it if you want to e.g.
  // to change the sample rate
  success &= (IMU.initializeDMP() == ICM_20948_Stat_Ok);

  // Enable the DMP orientation and accelerometer sensors
  success &= (IMU.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);
  success &= (IMU.enableDMPSensor(INV_ICM20948_SENSOR_ACCELEROMETER) == ICM_20948_Stat_Ok);

  // Configuring DMP to output data at multiple ODRs:
  success &= (IMU.setDMPODRrate(DMP_ODR_Reg_Quat9, 2) == ICM_20948_Stat_Ok);  // Set to the maximum
  success &= (IMU.setDMPODRrate(DMP_ODR_Reg_Accel, 2) == ICM_20948_Stat_Ok);  // Set to the maximum

  // Enable the FIFO
  success &= (IMU.enableFIFO() == ICM_20948_Stat_Ok);

  // Enable the DMP
  success &= (IMU.enableDMP() == ICM_20948_Stat_Ok);

  // Reset DMP
  success &= (IMU.resetDMP() == ICM_20948_Stat_Ok);

  // Reset FIFO
  success &= (IMU.resetFIFO() == ICM_20948_Stat_Ok);

  // Check success
  if (success) {
    SERIAL_PORT.println(F("DMP enabled!"));
  } else {
    SERIAL_PORT.println(F("Enable DMP failed!"));
  }

  // setup kalman filter
  kalmanvert.init(baro.altF,
                  0.0,
                  POSITION_MEASURE_STANDARD_DEVIATION,
                  ACCELERATION_MEASURE_STANDARD_DEVIATION,
                  millis());

  tPrev = millis();
}

void imu_update() {
  /*
  String accelName = "accel,";
  String accelEntry = accelName + String(at);
  Telemetry.writeText(accelEntry);
  */

  if (processQuaternion()) {
    // update kalman filter
    kalmanvert.update(baro.altF, accelVert * 9.80665f, millis());

    String kalmanName = "kalman,";
    String kalmanEntryString = kalmanName + String(kalmanvert.getPosition(), 8) + ',' +
                               String(kalmanvert.getVelocity(), 8) + ',' +
                               String(kalmanvert.getAcceleration(), 8);

    Telemetry.writeText(kalmanEntryString);
  }
}

float IMU_getAccel() {
  return accelTot;
}