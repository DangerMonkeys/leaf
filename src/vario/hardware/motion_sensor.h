#pragma once

#include "hardware/configuration.h"

#ifdef USE_LSM6DSV
#include "hardware/lsm6dsv.h"
using MotionSensor = LSM6DSV;
#else
#include "hardware/icm_20948.h"
using MotionSensor = ICM20948;
#endif
