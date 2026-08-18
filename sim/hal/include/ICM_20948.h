// SparkFun ICM-20948 stand-in.
//
// The IMU's motion data reaches the emulator as MotionUpdate messages, not as register reads: the
// part's DMP runs firmware of its own that produces the fused quaternions, and emulating that
// register-by-register would be emulating a second processor.  So hardware/icm_20948.cpp is
// replaced (see sim/device/sensors.cpp) and only the types its header names need to exist.
#pragma once

#include <stdint.h>

typedef enum {
  ICM_20948_Stat_Ok = 0,
  ICM_20948_Stat_Err,
  ICM_20948_Stat_NotImpl,
  ICM_20948_Stat_ParamErr,
  ICM_20948_Stat_WrongID,
  ICM_20948_Stat_InvalSensor,
  ICM_20948_Stat_NoData,
  ICM_20948_Stat_SensorNotSupported,
  ICM_20948_Stat_DMPNotSupported,
  ICM_20948_Stat_DMPVerifyFail,
  ICM_20948_Stat_FIFONoDataAvail,
  ICM_20948_Stat_FIFOIncompleteData,
  ICM_20948_Stat_FIFOMoreDataAvail,
  ICM_20948_Stat_UnrecognisedDMPHeader,
  ICM_20948_Stat_UnrecognisedDMPHeader2,
  ICM_20948_Stat_InvalDMPRegister,
} ICM_20948_Status_e;

typedef enum {
  INV_ICM20948_SENSOR_ACCELEROMETER = 0,
  INV_ICM20948_SENSOR_GYROSCOPE,
  INV_ICM20948_SENSOR_ORIENTATION,
} inv_icm20948_sensor;

typedef enum {
  DMP_ODR_Reg_Accel = 0,
  DMP_ODR_Reg_Gyro,
  DMP_ODR_Reg_Quat9,
} DMP_ODR_Registers;

struct icm_20948_DMP_data_t {
  uint16_t header = 0;
  uint16_t header2 = 0;
  struct {
    struct {
      int16_t X, Y, Z;
    } Accel;
    struct {
      int32_t Q1, Q2, Q3;
      uint16_t Accuracy;
    } Quat9;
  } Raw{};
};

// Values the firmware compares against when decoding a FIFO packet.
#define DMP_header_bitmap_Accel 0x8000
#define DMP_header_bitmap_Quat9 0x0400

class ICM_20948_I2C {
 public:
  ICM_20948_Status_e status = ICM_20948_Stat_Ok;

  const char* statusString(ICM_20948_Status_e code = ICM_20948_Stat_Ok) { return "emulated"; }
  void enableDebugging() {}
};
