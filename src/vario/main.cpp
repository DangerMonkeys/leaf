#include "Arduino.h"
#include "comms/ble.h"
#include "comms/fanet_radio.h"
#include "dispatch/message_bus.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/configuration.h"
#include "hardware/icm_20948.h"
#include "hardware/lc86g.h"
#include "hardware/ms5611.h"
#include "instruments/ambient.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "instruments/imu.h"
#include "logging/log.h"
#include "power.h"
#include "taskman.h"
#include "ui/settings/settings.h"

#ifdef DEBUG_WIFI
#include "comms/udp_message_server.h"
#endif

// MAIN Module
// initializes the system.  Responsible for setting up resources with
// as much dynamic memory as possible at the system bootup.  Sets up
// a message bus and hook the module's event routers into the bus

// Main message bus
MessageBus<10> bus;

void setup() {
  // Start USB Serial Debugging Port
  Serial.begin(115200);
  Serial.println("Starting Setup");

  // Initialize the shared bus
  spi_init();
  Serial.println(" - Finished SPI");

#ifdef FANET_CAPABLE
  FanetRadio::getInstance().setup(&bus);
#endif

  AHT20::getInstance().attach(&bus);
  ICM20948::getInstance().attach(&bus);
  lc86g.attach(&bus);
  ms5611.attach(&bus);

#ifdef DEBUG_WIFI
  udpMessageServer.attach(&bus);
#endif

  baro.subscribe(&bus);

  // Initialize anything left over on the Task Manager System
  Serial.println("Initializing Taskman Service");
  taskmanSetup();

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
  gps.attach(&bus);

  // Connect ambient environment instrument to message bus sourcing ambient environment updates
  ambient.subscribe(&bus);

  // Connect IMU instrument to message bus sourcing motion updates
  imu.subscribe(&bus);

  // Subscribe modules that need bus updates.
  // This should not exceed the bus router limit.
  bus.subscribe(BLE::get());

  // Provide logger access to the bus
  log_setBus(&bus);

  Serial.println("Leaf Initialized");
}
