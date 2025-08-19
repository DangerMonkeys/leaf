#include "instruments/imu.h"

#include "dispatch/message_types.h"
#include "hardware/Leaf_I2C.h"
#include "logging/log.h"
#include "logging/telemetry.h"
#include "math/kalman.h"
#include "storage/sd_card.h"

// Singleton IMU instance for device
IMU imu;

// === What to display on the serial port
// #define ALIGN_TEXT 3  // When set, puts values in equal-width columns with this many digits
// #define SHOW_QUATERNION  // When set, print the components of the orientation quaternion
// #define SHOW_DEVICE_ACCEL  // When set, print the components of the device-frame acceleration
// #define SHOW_WORLD_ACCEL  // When set, print the components of the world-frame acceleration
// #define SHOW_VERTICAL_ACCEL  // When set, print the vertical acceleration (with gravity removed)
// #define SHOW_FIXED_BOUNDS 0.2  // When set, print fixed values to keep the Arduino serial plot in
// a consistent range

#define IMU_STARTUP_CYCLES 80  // #samples to bypass at startup while accel calibrates

// == Estimation of constant gravity acceleration ==
const double NEW_MEASUREMENT_WEIGHT = 0.9;  // Weight given to new measurements...
const double AFTER_SECONDS = 5.0;           // ...after this number of seconds
const double K_UPDATE = log(1 - NEW_MEASUREMENT_WEIGHT) / AFTER_SECONDS;

void quaternionMult(double qw, double qx, double qy, double qz, double rw, double rx, double ry,
                    double rz, double* pw, double* px, double* py, double* pz) {
  *pw = rw * qw - rx * qx - ry * qy - rz * qz;
  *px = rw * qx + rx * qw - ry * qz + rz * qy;
  *py = rw * qy + rx * qz + ry * qw - rz * qx;
  *pz = rw * qz - rx * qy + ry * qx + rz * qw;
}

void rotateByQuaternion(double px, double py, double pz, double qw, double qx, double qy, double qz,
                        double* p1x, double* p1y, double* p1z) {
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

void IMU::processQuaternion(const MotionUpdate& m) {
  // Scale to +/- 1
  double magnitude = ((m.qx * m.qx) + (m.qy * m.qy) + (m.qz * m.qz));
  if (magnitude >= 1.0) magnitude = 1.0;
  double qw = sqrt(1.0 - magnitude);

  bool needComma = false;
  bool needNewline = false;

#ifdef SHOW_QUATERNION
  Serial.print(F("Qw:"));
  printFloat(qw);
  Serial.print(F(",Qx:"));
  printFloat(m.qx);
  Serial.print(F(",Qy:"));
  printFloat(m.qy);
  Serial.print(F(",Qz:"));
  printFloat(m.qz);
  needComma = true;
  needNewline = true;
#endif

  accelTot_ = sqrt(m.ax * m.ax + m.ay * m.ay + m.az * m.az);

#ifdef SHOW_DEVICE_ACCEL
  if (needComma) {
    Serial.print(',');
  }
  Serial.print(F("Ax:"));
  printFloat(m.ax);
  Serial.print(F(",Ay:"));
  printFloat(m.ay);
  Serial.print(F(",Az:"));
  printFloat(m.az);
  needComma = true;
  needNewline = true;
#endif

  double awx, awy, awz;
  rotateByQuaternion(m.ax, m.ay, m.az, qw, m.qx, m.qy, m.qz, &awx, &awy, &awz);

  accelVert_ = awz - zAvg_;

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

  double dt = ((double)m.t - tPrev_) * 0.001;
  double f = exp(K_UPDATE * dt);
  tPrev_ = m.t;

  zAvg_ = zAvg_ * f + awz * (1 - f);

  if (needNewline) {
    Serial.println();
  }
}

/*************************************************
 * Initialize motion processing with Kalman Filter *
 *************************************************/
void IMU::init() {
  startupCycleCount_ = IMU_STARTUP_CYCLES;

  // setup kalman filter
  kalmanvert_.init(millis() / 1000.0, baro.altF(), 0.0);

  tPrev_ = millis();
}

void IMU::on_receive(const MotionUpdate& msg) {
  processQuaternion(msg);

  // if we're starting up, block accel values until it's stable
  if (startupCycleCount_ > 0) {
    startupCycleCount_--;
    // submit accel = 0 to kalman filter and return
    kalmanvert_.update(millis() / 1000.0, baro.altF(), 0.0f);
    return;
  }

  // update kalman filter
  kalmanvert_.update(millis() / 1000.0, baro.altF(), accelVert_ * 9.80665f);

  String kalmanName = "kalman,";
  String kalmanEntryString = kalmanName + String(kalmanvert_.getPosition(), 8) + ',' +
                             String(kalmanvert_.getVelocity(), 8) + ',' +
                             String(kalmanvert_.getAcceleration(), 8);

  Telemetry.writeText(kalmanEntryString);
}

void IMU::wake() { startupCycleCount_ = IMU_STARTUP_CYCLES; }

float IMU::getAccel() { return accelTot_; }

float IMU::getVelocity() { return (float)kalmanvert_.getVelocity(); }
