#include "fanet_radio.h"
#include <RadioLib.h>
#include "FreeRTOS.h"
#include "configuration.h"
#include "esp_mac.h"
#include "etl/array.h"
#include "fanetGroundTracking.h"
#include "fanetManager.h"
#include "lock_guard.h"
#include "settings.h"

// Declare static variables
FanetRadioState FanetRadio::state = FanetRadioState::UNINITIALIZED;
SemaphoreHandle_t FanetRadio::x_fanet_manager_mutex = nullptr;
Fanet::FanetManager* FanetRadio::manager = nullptr;
SX1262* FanetRadio::radio = nullptr;
etl::array<uint8_t, 256> FanetRadio::buffer = {};
TaskHandle_t FanetRadio::x_fanet_rx_task = nullptr;
TaskHandle_t FanetRadio::x_fanet_tx_task = nullptr;
etl::optional<etl::delegate<void(Fanet::Packet&)>> FanetRadio::rx_callback = etl::nullopt;
volatile bool FanetRadio::last_was_tx = false;

#ifdef LORA_SX1262
auto radioModule = Module((uint32_t)SX1262_NSS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY);
#endif

ICACHE_RAM_ATTR void FanetRadio::onRxIsr() {
  // If this interrupt was called just to say a transmission has been completed,
  // there's no need to wake anyone up to process is
  if (last_was_tx) {
    last_was_tx = false;
    return;
  }

  // Required on the NotifyFromISR Callback.
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  // Interrupt that a packet has been received.  Notify the RadioRx task
  // that we have packet(s) to handle.
  xTaskNotifyFromISR(x_fanet_rx_task, 0, eNoAction, &xHigherPriorityTaskWoken);

  // Optionally, perform a context switch if a higher-priority task was woken
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/// @brief Responsible for reading packets from Radio and processing them in the manager
/// @param pvParameters
void FanetRadio::taskRadioRx(void* pvParameters) {
  while (true) {
    processRxPacket();

    // Notify the Tx task that there *may* be packets to process from the manager
    xTaskNotify(x_fanet_tx_task, 0, eNoAction);

    // Release the lock & Wait for the next notification of there being a packet
    // to process
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

void FanetRadio::processRxPacket() {
  auto& radio = *FanetRadio::radio;

  // Take the radio mutex
  LockGuard lock(x_fanet_manager_mutex);

  auto length = radio.getPacketLength();

  // If a 0 length packet is here, I'm guessing the "done" DIO pin was fired
  // without there being any data to process.  Just clear this and continue on
  // our way for the next notification.
  if (length == 0) {
    return;
  }

  // A packet is able to be read
  auto rxState = radio.readData(buffer.data(), length);
  if (rxState == RADIOLIB_ERR_NONE) {
    // A packet was received successfully.  Process it in our Fanet manager
    auto optPacket = manager->handleRx(buffer, length, millis(), radio.getRSSI(), radio.getSNR());

    if (optPacket.has_value()) {
      // If this packet is intended for our application, produce the callback
      auto& packet = optPacket.value();
      if (rx_callback.has_value()) {
        rx_callback.value()(packet);
      }
    }
  }
}

void FanetRadio::taskRadioTx(void* pvParameters) {
  auto& manager = *FanetRadio::manager;
  auto& radio = *FanetRadio::radio;

  while (true) {
    auto sleepTill = portMAX_DELAY;
    // Take the radio mutex
    {
      LockGuard lock(x_fanet_manager_mutex);

      // Loop through all of the packets waiting in the tx queue
      // to be send out at the current time
      auto currentTime = millis();
      auto nextTxTime = manager.nextTxTime(currentTime);
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

      // Figure out how long it is we need to sleep until
      if (nextTxTime.has_value()) {
        sleepTill = pdMS_TO_TICKS(nextTxTime.value() - currentTime);
      }
    }

    // Release the mutex and go back to sleep, waiting for our next time to
    // check for queued packets
    // ulTaskNotifyTake(pdTRUE, sleepTill); // TODO:  This
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
  }
}

void FanetRadio::begin(const FanetRadioRegion& region) {
#ifndef FANET
  return;  // Model does not support Fanet
#endif

  // End the radio module if it's already running
  end();

  // If the region is set not to broadcast, turn off.
  if (region == FanetRadioRegion::OFF) {
    return;
  }

  state = FanetRadioState::INITIALIZING;

  // Create the mutex, lock it.
  x_fanet_manager_mutex = xSemaphoreCreateMutex();
  if (x_fanet_manager_mutex == NULL) {
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

  LockGuard lock(x_fanet_manager_mutex);

#ifdef LORA_SX1262
  radio = new SX1262(&radioModule);
#endif

  // Set the radio callback ISR
  radio->setPacketReceivedAction(onRxIsr);

  int16_t radioInitState = RADIOLIB_ERR_UNKNOWN;

  // Initialize the radio for the settings of the given region
  switch (region) {
    case FanetRadioRegion::US:
      radioInitState = radio->begin(920.800f, 500.0f, 7U, 5U, 0xF1, 10U, 8U, 1.6f, false);
      break;
  }

  if (radioInitState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

  if (radio->setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

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

  // Create the TX Task
  auto taskCreateCode = xTaskCreate(taskRadioTx,
                                    "FanetTx",
                                    4096,
                                    nullptr,
                                    1,  // Typical lower priority task
                                    &x_fanet_tx_task);
  if (taskCreateCode != pdPASS) {
    Serial.println((String) "Creating Tx task failed: " + taskCreateCode);
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

  // Create the RX Task
  taskCreateCode = xTaskCreate(taskRadioRx,
                               "FanetXx",
                               4096,
                               nullptr,
                               1,  // Typical lower priority task
                               &x_fanet_rx_task);
  if (taskCreateCode != pdPASS) {
    Serial.print((String) "Creating Rx task failed: " + taskCreateCode);
    state = FanetRadioState::FAILED_OTHER;
    return;
  }

  auto rxState = radio->startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Radio->startReceive failed");
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

  // We're finished, release the locks.
  state = FanetRadioState::RUNNING;
}

void FanetRadio::end() {
  // Destroy any running tasks
  if (x_fanet_rx_task != nullptr) {
    vTaskDelete(x_fanet_rx_task);
    x_fanet_rx_task = nullptr;
  }
  if (x_fanet_tx_task != nullptr) {
    vTaskDelete(x_fanet_tx_task);
    x_fanet_tx_task = nullptr;
  }

  // Destroy any mutex that was created
  if (x_fanet_manager_mutex != nullptr) {
    vSemaphoreDelete(x_fanet_manager_mutex);
    x_fanet_manager_mutex = nullptr;
  }

  // Destroy the manager
  if (manager != nullptr) {
    delete manager;
    manager = nullptr;
  }

  // Destroy the radio
  if (radio != nullptr) {
    // Not sure the deconstructor will explicitly sleep the radio.
    radio->sleep(false);
    delete radio;
    radio = nullptr;
  }

  state = FanetRadioState::UNINITIALIZED;
}

FanetRadioState FanetRadio::getState() {
  return state;
}

void FanetRadio::setCurrentLocation(const float& lat,
                                    const float& lon,
                                    const uint32_t& alt,
                                    const int& heading,
                                    const float& climbRate,
                                    const float& speedKmh) {
  if (state != FanetRadioState::RUNNING) {
    return;
  }
  // Acquire the manager lock, notify the manager
  LockGuard lock(x_fanet_manager_mutex);
  manager->setPos(lat, lon, alt, millis(), heading, climbRate, speedKmh);
}

void FanetRadio::setRxCallback(etl::delegate<void(Fanet::Packet&)> val) {
  rx_callback = val;
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
  snprintf(buffer,
           sizeof(buffer),
           "%02X%02X%02X",
           address.manufacturer,
           address.device << 8,
           address.device | 0xFF);
  buffer[6] = '\0';
  auto ret = String(buffer);
  ret.toUpperCase();
  return ret;
}