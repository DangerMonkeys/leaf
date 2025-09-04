#include "Arduino.h"
#include "comms/ble.h"
#include "comms/fanet_radio.h"
#include "diagnostics/buttons.h"
#include "dispatch/message_bus.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/buttons.h"
#include "hardware/configuration.h"
#include "hardware/icm_20948.h"
#include "hardware/lc86g.h"
#include "hardware/ms5611.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/buslog.h"
#include "power.h"
#include "taskman.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/input/button_dispatcher.h"
#include "ui/settings/settings.h"
#include "wind_estimate/wind_estimate.h"

#ifdef DEBUG_WIFI
#include "comms/udp_message_server.h"
#endif

// MAIN Module
// initializes the system.  Responsible for setting up resources with
// as much dynamic memory as possible at the system bootup.  Sets up
// a message bus and hook the module's event routers into the bus

// Main message bus
MessageBus<11> bus;

TaskManager taskman;

void setup() {
  // Start USB Serial Debugging Port
  Serial.begin(115200);
  Serial.println("Starting Setup");

  // Initialize the shared bus
  spi_init();
  Serial.println(" - Finished SPI");

#ifdef FANET_CAPABLE
  fanetRadio.subscribe(&bus);
  fanetRadio.setup();
  fanetRadio.publishTo(&bus);
#endif

  // grab user settings (or populate defaults if no saved settings)
  settings.init();

  buttons.publishTo(&bus);

  if (!settings.dev_startDisconnected) {
    Serial.println("Connecting hardware devices to bus");
    AHT20::getInstance().publishTo(&bus);
    ICM20948::getInstance().publishTo(&bus);
    lc86g.publishTo(&bus);
    ms5611.publishTo(&bus);
  } else {
    Serial.println("Leaving hardware devices unconnected to bus");
  }

#ifdef DEBUG_WIFI
  udpMessageServer.publishTo(&bus);
#endif

  baro.subscribe(&bus);
  baro.publishTo(&bus);

  // Initialize anything left over on the Task Manager System
  Serial.println("Initializing Taskman Service");
  taskman.init();

  // Initialize the BLE Stack, subscribe it to events
  // from the message bus.
  Serial.println("Initializing Bluetooth Module");
  BLE::get().setup();
  if (settings.system_bluetoothOn) {
    BLE::get().start();
  }

  // Connect GPS instrument to message bus sourcing lines of text that should be NMEA sentences
  gps.subscribe(&bus);
  // Publish parsed GPS messages to message bus
  gps.publishTo(&bus);

  // Connect ambient environment instrument to message bus sourcing ambient environment updates
  ambient.subscribe(&bus);

  // Connect IMU instrument to message bus sourcing motion updates
  imu.subscribe(&bus);
  imu.publishTo(&bus);

  windEstimator.subscribe(&bus);

  // Subscribe modules that need bus updates.
  // This should not exceed the bus router limit.
  bus.subscribe(BLE::get());

  buttonMonitor.subscribe(&bus);
  buttonDispatcher.subscribe(&bus);

  // Provide bus logger access to the bus
  busLog.setBus(&bus);

  Serial.println("Leaf Initialized");
}

void loop() { taskman.update(); }
