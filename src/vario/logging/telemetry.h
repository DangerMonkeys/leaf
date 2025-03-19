#pragma once

#include "storage/files.h"

/*
 *  This is a VERY basic CSV telemetry module for logging data.
 *  It currently works by writing data into a csv file every second
 *  This WILL be re-factored into a binary file format for very quick logging
 *  at some point
 */
class Telemetry_t {
 public:
  Telemetry_t() {}
  bool begin();
  void end();
  void writeText(const String text);

 private:
  File file;
};

extern Telemetry_t Telemetry;