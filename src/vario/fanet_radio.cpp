#include "fanet_radio.h"
#include "FreeRTOS.h"
#include "Leaf_SPI.h"
#include "baro.h"
#include "configuration.h"
#include "esp_mac.h"
#include "etl/array.h"
#include "fanetGroundTracking.h"
#include "fanetManager.h"
#include "lock_guard.h"
#include "log.h"
#include "message_types.h"

// Static initializers
volatile bool FanetRadio::last_was_tx = false;

ICACHE_RAM_ATTR void FanetRadio::onRxIsr() {
  auto& fanet = FanetRadio::getInstance();
  // If this interrupt was called just to say a transmission has been completed,

  // there's no need to wake anyone up to process is
  // Try and clear the receive?
  if (FanetRadio::last_was_tx) {
    FanetRadio::last_was_tx = false;
    return;
  }

  // Required on the NotifyFromISR Callback.
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  // Interrupt that a packet has been received.  Notify the RadioRx task
  // that we have packet(s) to handle.
  xTaskNotifyFromISR(fanet.x_fanet_rx_task, 0, eNoAction, &xHigherPriorityTaskWoken);

  // Optionally, perform a context switch if a higher-priority task was woken
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void FanetRadio::taskRadioNameTx(void* pvParameters) {
  auto& fanet = FanetRadio::getInstance();
  Fanet::Name namePayload;
  namePayload.name = "Leaf";

  while (true) {
    if (fanet.state == FanetRadioState::RUNNING) {
      auto guard = LockGuard(fanet.x_fanet_manager_mutex);
      fanet.manager->sendPacket(namePayload, millis());
      xTaskNotify(fanet.x_fanet_tx_task, 0, eNoAction);
    }
    delay(1000);
  }
}

/// @brief Responsible for reading packets from Radio and processing them in the manager
/// @param pvParameters
void FanetRadio::taskRadioRx(void* pvParameters) {
  auto& fanet = FanetRadio::getInstance();

  while (true) {
    if (fanet.state == FanetRadioState::RUNNING) {
      fanet.processRxPacket();

      // Notify the Tx task that there *may* be packets to process from the manager
      xTaskNotify(fanet.x_fanet_tx_task, 0, eNoAction);
    }

    // Release the lock & Wait for the next notification of there being a packet
    // to process
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

void FanetRadio::processRxPacket() {
  auto& radio = *FanetRadio::radio;

  int16_t rxState;
  size_t length;
  if (SpiLockGuard()) {
    length = radio.getPacketLength();

    // If a 0 length packet is here, I'm guessing the "done" DIO pin was fired
    // without there being any data to process.  Just clear this and continue on
    // our way for the next notification.
    if (length == 0) {
      return;
    }

    // A packet is able to be read
    rxState = radio.readData(buffer.data(), length);
  }

  if (rxState == RADIOLIB_ERR_NONE) {
    auto rssi = radio.getRSSI();
    auto snr = radio.getSNR();

    // A packet was received successfully.  Process it in our Fanet manager
    etl::optional<Fanet::Packet> optPacket;
    if (LockGuard(x_fanet_manager_mutex))
      optPacket = manager->handleRx(buffer, length, millis(), rssi, snr);

    if (optPacket.has_value()) {
      // If this packet is intended for our application, produce the callback
      auto& packet = optPacket.value();

      // Put the packet on the bus
      bus->receive(FanetPacket(packet, rssi, snr));
    }
  }
}

void FanetRadio::taskRadioTx(void* pvParameters) {
  auto& fanetRadio = FanetRadio::getInstance();
  auto& manager = *fanetRadio.manager;
  auto& radio = *fanetRadio.radio;

  while (true) {
    // Default to 2 seconds of sleep to check the send queue
    // and perform neighbor flushing events.
    auto sleepTill = pdMS_TO_TICKS(2000);
    // Take the radio & SPI mutex
    if (fanetRadio.state == FanetRadioState::RUNNING) {
      // Very important to take the spiLock before the radio lock here!
      // The draw methods may take out a lock on the SPI bus while they're
      // requesting information from our Fanet Manager
      SpiLockGuard spiLock;
      LockGuard lock(fanetRadio.x_fanet_manager_mutex);

      // Loop through all of the packets waiting in the tx queue
      // to be send out at the current time
      auto currentTime = millis();
      etl::optional<unsigned long> nextTxTime = etl::nullopt;
      nextTxTime = manager.nextTxTime(currentTime);

      while (nextTxTime.has_value() && nextTxTime.value() <= currentTime) {
        manager.doTx(currentTime,
                     [&](const etl::array<uint8_t, 256>* bytes, const size_t& size) -> bool {
                       // Closure to send the requested from the packet on the wire.
                       // Ordering is important here as the ISR will be called before the end
                       // if the radio.transmit method.
                       last_was_tx = true;
                       auto txResult = radio.transmit(bytes->data(), size) == RADIOLIB_ERR_NONE;
                       return txResult;
                     });
        currentTime = millis();
        nextTxTime = manager.nextTxTime(currentTime);
      };
      radio.startReceive();

      // Flush out the neighbor table every ~30 seconds
      if (currentTime - fanetRadio.neighbor_table_flushed > 30000) {
        manager.flushOldNeighborEntries(currentTime);
        fanetRadio.neighbor_table_flushed = currentTime;
      }

      // Figure out how long it is we need to sleep until
      if (nextTxTime.has_value()) {
        sleepTill = pdMS_TO_TICKS(nextTxTime.value() - currentTime);
      }
    }

    // Release the mutex and go back to sleep, waiting for our next time to
    // check for queued packets.  Run this every 2 seconds as a sanity
    // check
    ulTaskNotifyTake(pdTRUE, sleepTill);
  }
}

void FanetRadio::setupFanetHandler() {
  // Figure out what SRC address to use.
  auto addressString = getAddress();
  Fanet::Mac srcAddress;

  // Convert the 6 character arduino hex string into 3 bytes
  uint8_t addressBytes[3];
  for (int i = 0; i < 3; i++) {
    addressBytes[i] = strtol(addressString.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }
  srcAddress.manufacturer = addressBytes[0];
  srcAddress.device = (addressBytes[1] << 8) | addressBytes[2];

  // Initialize the FANet Manager
  manager = new Fanet::FanetManager(srcAddress, millis());

  // TODO:  Probably move this into a configurable setting
  manager->aircraftType = Fanet::AircraftType::Paraglider;
}

void FanetRadio::setup(etl::imessage_bus* bus) {
  // Sets up the radio module.  Leaves it in an uninitialized state, but
  // creates any dynamic memory required.
  this->bus = bus;

  // Take out a lock on the SPI bus.
  SpiLockGuard spiLock;

  // Create the mutex, lock it.
  x_fanet_manager_mutex = xSemaphoreCreateMutex();
  if (x_fanet_manager_mutex == NULL) {
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

#ifdef LORA_SX1262
  radio = new SX1262(&radioModule);

#endif

  // Set the radio callback ISR
  radio->setPacketReceivedAction(onRxIsr);

  // Sets up the FanetHandler, reserving memory if we were to
  // enable it.
  setupFanetHandler();

  // Create the TX Task
  auto taskCreateCode = xTaskCreate(taskRadioTx, "FanetTx", 4096, nullptr,
                                    1,  // Typical lower priority task
                                    &x_fanet_tx_task);
  if (taskCreateCode != pdPASS) {
    Serial.println((String) "Creating Tx task failed: " + taskCreateCode);
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

  // Create the RX Task
  taskCreateCode = xTaskCreate(taskRadioRx, "FanetRx", 4096, nullptr,
                               1,  // Typical lower priority task
                               &x_fanet_rx_task);
  if (taskCreateCode != pdPASS) {
    Serial.print((String) "Creating Rx task failed: " + taskCreateCode);
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

  // TODO:  Add a setting for sending out periodic Fanet names
  // Create the name TX task
  // Just disable this for now until we support names :)
  // taskCreateCode =
  //     xTaskCreate(taskRadioNameTx, "FanetNameTx", 4096, nullptr, 1, &x_fanet_tx_name_task);

  // Put the radio to sleep once it has been initialized.
  radio->sleep();
}

void FanetRadio::begin(const FanetRadioRegion& region) {
#ifndef FANET
  return;  // Model does not support Fanet
#endif

  // Short circuit above taking any locks out (avoid deadlocks)
  if (region == FanetRadioRegion::OFF) {
    end();
    return;
  }

  state = FanetRadioState::INITIALIZING;
  int16_t radioInitState = RADIOLIB_ERR_UNKNOWN;
  // Always take the SPI lock out before the Fanet Manager lock
  // to avoid deadlocks with janky display modules locking the SPI
  // bus and making Fanet state changes or requests after.
  SpiLockGuard spiLock;
  LockGuard lock(x_fanet_manager_mutex);

  Serial.println("[FanetRadio] Initializing");
  // Initialize the radio for the settings of the given region

  switch (region) {
    case FanetRadioRegion::US:
      radioInitState = radio->begin(920.800f, 500.0f, 7U, 5U, 0xF1, 22U, 8U, 1.8f, false);
      break;
  }

  if (radioInitState != RADIOLIB_ERR_NONE) {
    Serial.printf("[FanetRadio] Module initialization failed: %d\n", radioInitState);
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

  Serial.println("[FanetRadio] Initialized");

  auto rxState = radio->startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Radio->startReceive failed");
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

#ifdef LORA_SX1262
  radio->setRfSwitchPins(SX1262_RF_SW, RADIOLIB_NC);
  if (radio->setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }
  // Try to get a few extra db
  radio->setRxBoostedGainMode(true, true);
#endif

  // We're finished, release the locks.
  state = FanetRadioState::RUNNING;
}

void FanetRadio::end() {
  SpiLockGuard spiLock;
  radio->sleep(false);
  state = FanetRadioState::UNINITIALIZED;
}

FanetRadioState FanetRadio::getState() { return state; }

void FanetRadio::setCurrentLocation(const float& lat, const float& lon, const uint32_t& alt,
                                    const int& heading, const float& climbRate,
                                    const float& speedKmh) {
  if (state != FanetRadioState::RUNNING) {
    return;
  }
  // Acquire the manager lock, notify the manager
  LockGuard lock(x_fanet_manager_mutex);
  manager->setPos(lat, lon, alt, millis(), heading, climbRate, speedKmh);

  // Notify the Tx task that there *may* be packets to process from the manager
  xTaskNotify(x_fanet_tx_task, 0, eNoAction);
}

Fanet::Stats FanetRadio::getStats() {
  if (state != FanetRadioState::RUNNING) return Fanet::Stats();

  LockGuard lock(x_fanet_manager_mutex);
  return manager->getStats();
}

etl::unordered_map<uint32_t, Fanet::Neighbor, FANET_MAX_NEIGHBORS> FanetRadio::getNeighborTable() {
  if (state != FanetRadioState::RUNNING)
    return etl::unordered_map<uint32_t, Fanet::Neighbor, FANET_MAX_NEIGHBORS>();

  LockGuard lock(x_fanet_manager_mutex);
  return manager->getNeighborTable();
}

void FanetRadio::on_receive(const GpsReading& msg) {
  // Not a valid GPS location.  Bail out
  if (!msg.gps.location.isValid()) return;

  if (!manager->getGroundType().has_value() && !getAreWeFlying()) {
    // We're not performing ground tracking, and we're not currently flying.
    // Bail out.
    return;
  }

  // Update the FANet radio module of our current location
  TinyGPSPlus gps = msg.gps;  // Needed as lat() calls are not const :'(
  setCurrentLocation(gps.location.lat(), gps.location.lng(), gps.altitude.meters(),
                     gps.course.deg(), baro.climbRate / 100.0f, gps.speed.kmph());
}

String FanetRadio::getAddress() {
  // Checks if we have a preference for Fanet IDs
  if (FANET_address.isEmpty()) {
    // Find the mac address for our ESP32.  The spec mentions using 0xFB for
    // generic ESP32 devices, so, we'll use this for anything without a Leaf
    // ID allocated and use the last 2 bytes of the Mac address.

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    String ret = "FB";
    ret += String(mac[4] < 16 ? "0" : "") + String(mac[4], HEX);
    ret += String(mac[5] < 16 ? "0" : "") + String(mac[5], HEX);
    ret.toUpperCase();
    return ret;
  }
  return FANET_address;
}

void FanetRadio::setTrackingMode(const etl::optional<Fanet::GroundTrackingType::enum_type>& mode) {
  if (state != FanetRadioState::RUNNING) {
    return;
  }
  // Acquire the manager lock, notify the manager
  LockGuard lock(x_fanet_manager_mutex);
  manager->setGroundType(mode);
}

String MacToString(Fanet::Mac address) {
  char buffer[7];
  snprintf(buffer, sizeof(buffer), "%02X%02X%02X", address.manufacturer, address.device << 8,
           address.device | 0xFF);
  buffer[6] = '\0';
  auto ret = String(buffer);
  ret.toUpperCase();
  return ret;
}