/* Demonstrates setting up and reading from the IMU to estimate world-frame vertical acceleration.
 *
 * Control what is printed to Serial by commenting and uncommenting the options under "What to
 * display on the serial port" below.  When ALIGN_TEXT is commented, the Serial output is suitable
 * to be viewed with the Arduino Serial Plotter (squiggly button in upper right).
 */

#include <ICM_20948.h>
#include "gravity_estimator.h"

// === What to display on the serial port
// #define ALIGN_TEXT 3  // When set, puts values in equal-width columns with this many digits
// #define SHOW_QUATERNION  // When set, print the components of the orientation quaternion
// #define SHOW_DEVICE_ACCEL  // When set, print the components of the device-frame acceleration
// #define SHOW_WORLD_ACCEL  // When set, print the components of the world-frame acceleration
#define SHOW_VERTICAL_ACCEL  // When set, print the vertical acceleration (with gravity removed)
#define SHOW_FIXED_BOUNDS 0.2  // When set, print fixed values to keep the Arduino serial plot in a consistent range

#define AD0_VAL 0 // The value of the last bit of the I2C address.

ICM_20948_I2C myICM; // Otherwise create an ICM_20948_I2C object

GravityEstimator gravityEstimator(0.9, 5.0);
double zAvg = 1.0;  // Best guess for strength of gravity
uint32_t tPrev;  // Last time gravity guess was updated
const double NEW_MEASUREMENT_WEIGHT = 0.9;  // Weight given to new measurements...
const double AFTER_SECONDS = 5.0;  // ...after this number of seconds
const double K_UPDATE = log(1 - NEW_MEASUREMENT_WEIGHT) / AFTER_SECONDS;

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

// https://math.stackexchange.com/a/1721393/921079
void quaternionMult(double qw, double qx, double qy, double qz,
                    double rw, double rx, double ry, double rz,
                    double* pw, double* px, double* py, double* pz)
{
  *pw = rw*qw-rx*qx-ry*qy-rz*qz;
  *px = rw*qx+rx*qw-ry*qz+rz*qy;
  *py = rw*qy+rx*qz+ry*qw-rz*qx;
  *pz = rw*qz-rx*qy+ry*qx+rz*qw;
}

void rotateByQuaternion(double px, double py, double pz,
                        double qw, double qx, double qy, double qz,
                        double* p1x, double* p1y, double* p1z)
{
  double qrw, qrx, qry, qrz, qcw;
  quaternionMult(qw, qx, qy, qz,
                 0, px, py, pz,
                 &qrw, &qrx, &qry, &qrz);
  quaternionMult(qrw, qrx, qry, qrz,
                 qw, -qx, -qy, -qz,
                 &qcw, p1x, p1y, p1z);
}

void setup()
{
  Serial.begin(115200);
  Serial.println(F("leaf imu_test"));

  delay(100);

  while (Serial.available())
    Serial.read();

  Serial.println(F("Press any key to continue..."));
  Serial.flush();

  while (!Serial.available())
    ;

  Wire.begin();
  Wire.setClock(400000);

  bool initialized = false;
  while (!initialized)
  {
    // Initialize the ICM-20948
    // If the DMP is enabled, .begin performs a minimal startup. We need to configure the sample mode etc. manually.
    myICM.begin(Wire, AD0_VAL);

    Serial.print(F("Initialization of the sensor returned: "));
    Serial.println(myICM.statusString());
    if (myICM.status != ICM_20948_Stat_Ok)
    {
      Serial.println(F("Trying again..."));
      delay(500);
    }
    else
    {
      initialized = true;
    }
  }

  Serial.println(F("Device connected!"));

  bool success = true;

  // Initialize the DMP
  success &= (myICM.initializeDMP() == ICM_20948_Stat_Ok);

  // Enable the DMP orientation and accelerometer sensors
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ACCELEROMETER) == ICM_20948_Stat_Ok);

  // Configure the output data rate
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Quat9, 0) == ICM_20948_Stat_Ok); // Set to the maximum
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Accel, 0) == ICM_20948_Stat_Ok); // Set to the maximum

  // Enable the FIFO
  success &= (myICM.enableFIFO() == ICM_20948_Stat_Ok);

  // Enable the DMP
  success &= (myICM.enableDMP() == ICM_20948_Stat_Ok);

  // Reset DMP
  success &= (myICM.resetDMP() == ICM_20948_Stat_Ok);

  // Reset FIFO
  success &= (myICM.resetFIFO() == ICM_20948_Stat_Ok);

  // Check success
  if (success)
  {
    Serial.println(F("DMP enabled!"));
  }
  else
  {
    Serial.println(F("Enable DMP failed!"));
    Serial.println(F("Please check that you have uncommented line 29 (#define ICM_20948_USE_DMP) in ICM_20948_C.h..."));
    while (1)
      ;
  }

  tPrev = millis();
}

void loop()
{
  icm_20948_DMP_data_t data;
  myICM.readDMPdataFromFIFO(&data);

  if ((myICM.status == ICM_20948_Stat_Ok) || (myICM.status == ICM_20948_Stat_FIFOMoreDataAvail)) // Was valid data available?
  {
    if ((data.header & DMP_header_bitmap_Quat9) > 0 && (data.header & DMP_header_bitmap_Accel) > 0)
    {
      uint32_t t = millis();

      // Get quaternion that rotates device-frame vectors to world-frame
      // Scale to +/- 1
      double qx = ((double)data.Quat9.Data.Q1) / 1073741824.0; // Convert to double. Divide by 2^30
      double qy = ((double)data.Quat9.Data.Q2) / 1073741824.0; // Convert to double. Divide by 2^30
      double qz = ((double)data.Quat9.Data.Q3) / 1073741824.0; // Convert to double. Divide by 2^30
      double xyzMag2 = (qx * qx) + (qy * qy) + (qz * qz);
      double qw = xyzMag2 <= 1 ? sqrt(1.0 - xyzMag2) : 0;

      // Get device-frame acceleration
      double ax = ((double)data.Raw_Accel.Data.X) / 8192.0;
      double ay = ((double)data.Raw_Accel.Data.Y) / 8192.0;
      double az = ((double)data.Raw_Accel.Data.Z) / 8192.0;

      // Find world-frame acceleration (awz = acceleration, world frame, z direction = vertical acceleration)
      double awx, awy, awz;
      rotateByQuaternion(ax, ay, az, qw, qx, qy, qz, &awx, &awy, &awz);

      // Print information to Serial
      bool needComma = false;
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
      #endif

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
      #endif

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
      #endif

      #ifdef SHOW_VERTICAL_ACCEL
        if (needComma) {
          Serial.print(',');
        }
        Serial.print("dAz:");
        printFloat(awz - gravityEstimator.estimate());
        needComma = true;
      #endif

      #ifdef SHOW_FIXED_BOUNDS
        if (needComma) {
          Serial.print(',');
        }
        Serial.print("min:");
        printFloat(SHOW_FIXED_BOUNDS);
        Serial.print(",max:");
        printFloat(-SHOW_FIXED_BOUNDS);
      #endif

      Serial.println();

      // Update gravity estimator with measured vertical acceleration
      gravityEstimator.update(t, awz);
    }
  }

  if (myICM.status != ICM_20948_Stat_FIFOMoreDataAvail) // If more data is available then we should read it right away - and not delay
  {
    delay(10);
  }
}
