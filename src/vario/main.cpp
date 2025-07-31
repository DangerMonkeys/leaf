#include "Arduino.h"
#include "comms/ble.h"
#include "comms/fanet_radio.h"
#include "dispatch/message_bus.h"
#include "hardware/Leaf_SPI.h"
#include "hardware/aht20.h"
#include "hardware/configuration.h"
#include "instruments/ambient.h"
#include "instruments/gps.h"
#include "power.h"
#include "taskman.h"
#include "ui/settings/settings.h"

// MAIN Module
// initializes the system.  Responsible for setting up resources with
// as much dynamic memory as possible at the system bootup.  Sets up
// a message bus and hook the module's event routers into the bus

// Main message bus
MessageBus<10> bus;

void setup() {
  // Start USB Serial Debugging Port
  Serial.begin(115200);
  uint8_t counter = 0;
  while (!Serial && counter < 10) {
    delay(5 * counter++);
  }
  Serial.println("Starting Setup");

  // Initialize the shared bus
  spi_init();
  Serial.println(" - Finished SPI");

#ifdef FANET_CAPABLE
  FanetRadio::getInstance().setup(&bus);
#endif

  AHT20::getInstance().attach(&bus);

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

  // GPS Bus Interaction.
  // Still needs to be moved away from taskman, but we can
  // hook it into the message bus.
  gps.setBus(&bus);

  // Connect ambient environment instrument to message bus sourcing ambient environment updates
  ambient.subscribe(&bus);

  // Subscribe modules that need bus updates.
  // This should not exceed the bus router limit.
  bus.subscribe(BLE::get());

  Serial.println("Leaf Initialized");
}
