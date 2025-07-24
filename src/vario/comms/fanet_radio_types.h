#pragma once

#include <Arduino.h>
#include "etl/array.h"
#include "etl/enum_type.h"

struct FanetRadioRegion {
  enum enum_type { OFF = 0, US, EUROPE };
  static constexpr size_t size = 3;

  ETL_DECLARE_ENUM_TYPE(FanetRadioRegion, uint8_t)
  ETL_ENUM_TYPE(OFF, "OFF")
  ETL_ENUM_TYPE(US, "US")
  ETL_ENUM_TYPE(EUROPE, "EUROPE")
  ETL_END_ENUM_TYPE

 public:
  static constexpr etl::array<const char*, size> strings{"OFF", "US", "EUROPE"};
  static constexpr etl::array<uint8_t, size> values{0, 2};
};

struct FanetRadioState {
  enum enum_type {
    // A radio initialization has failed to detect the presence of
    // the FANET radio.  Presumably, the device is missing, or
    // failed.
    UNINSTALLED,

    // Radio is sitting idle, no initialization attempt
    UNINITIALIZED,

    // Radio is initializing
    INITIALIZING,

    // Radio initialization failed (eg. SPI bus failure)
    FAILED_RADIO_INIT,

    // Failed for another reason (needs Serial debugging?)
    FAILED_OTHER,

    // Radio is running and ready to rx/tx messages
    RUNNING,
  };
  static constexpr size_t size = 5;

  ETL_DECLARE_ENUM_TYPE(FanetRadioState, uint8_t)
  ETL_ENUM_TYPE(UNINITIALIZED, "UNINITIALIZED")
  ETL_ENUM_TYPE(INITIALIZING, "INITIALIZING")
  ETL_ENUM_TYPE(FAILED_RADIO_INIT, "FAILED_RADIO_INIT")
  ETL_ENUM_TYPE(FAILED_OTHER, "FAILED_OTHER")
  ETL_ENUM_TYPE(RUNNING, "RUNNING")
  ETL_END_ENUM_TYPE
};
