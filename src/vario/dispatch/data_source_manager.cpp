#include "dispatch/data_source_manager.h"

#include "dispatch/message_types.h"
#include "hardware/aht20.h"

DataSourceManager dataSourceManager;

void DataSourceManager::pollAmbient() {
  if (!ambientSource_ || !bus_) {
    return;
  }

  AmbientUpdateResult result = ambientSource_->update();
  if (result != AmbientUpdateResult::NoChange && result != AmbientUpdateResult::None) {
    float temp = ambientSource_->getTemp();
    float relRH = ambientSource_->getHumidity();
    bus_->receive(AmbientUpdate(temp, relRH, result));
  }
}
