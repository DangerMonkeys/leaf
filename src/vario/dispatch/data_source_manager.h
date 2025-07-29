#pragma once

#include "hardware/ambient_source.h"

#include "etl/message_bus.h"

class DataSourceManager {
 public:
  void attach(etl::imessage_bus* bus) { bus_ = bus; }
  void attach(IAmbientSource* ambientSource) { ambientSource_ = ambientSource; }
  void pollAmbient();

 private:
  etl::imessage_bus* bus_ = nullptr;
  IAmbientSource* ambientSource_ = nullptr;
};

// Singleton instance managing data source polling and dispatch to message bus
extern DataSourceManager dataSourceManager;
