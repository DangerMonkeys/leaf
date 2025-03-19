#include "Arduino.h"
#include "Leaf_SPI.h"
#include "ble.h"
#include "configuration.h"
#include "fanet_radio.h"
#include "gps.h"
#include "message_bus.h"
#include "settings.h"
#include "taskman.h"

// MAIN Module
// initializes the system.  Responsible for setting up resources with
// as much dynamic memory as possible at the system bootup.  Sets up
// a message bus and hook the module's event routers into the bus

// Main message bus
MessageBus<10> bus;

void setup() {
  // Start USB Serial Debugging Port
  Serial.begin(115200);
  delay(200);
  Serial.println("Starting Setup");
  delay(200);

  // Initialize the shared bus
  spi_init();
  Serial.println(" - Finished SPI");

#ifdef FANET
  // Initialize the Fanet Radio module.  Subscribe them for bus
  // updates
  FanetRadio::getInstance().setup(&bus);
#endif

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

  // Subscribe modules that need bus updates.
  // This should not exceed the bus router limit.
  bus.subscribe(BLE::get());
#ifdef FANET
  bus.subscribe(FanetRadio::getInstance());
#endif

  Serial.println("Leaf Initialized");
}
