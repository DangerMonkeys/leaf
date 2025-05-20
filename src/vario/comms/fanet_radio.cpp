#include "comms/fanet_radio.h"

#include "FreeRTOS.h"
#include "comms/message_types.h"
#include "esp_mac.h"
#include "etl/array.h"
#include "fanet/name.hpp"
#include "fanet/packetParser.hpp"
#include "fanet/tracking.hpp"
#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "instruments/baro.h"
#include "logging/log.h"
#include "utils/lock_guard.h"

// Static initializers
volatile bool FanetRadio::frameSending = false;

ICACHE_RAM_ATTR void FanetRadio::onRxIsr() {
  auto& fanet = FanetRadio::getInstance();
  // If this interrupt was called just to say a transmission has been completed,

  // there's no need to wake anyone up to process is
  // Try and clear the receive?
  if (FanetRadio::frameSending) {
    FanetRadio::frameSending = false;
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

  while (true) {
    if (fanet.state == FanetRadioState::RUNNING) {
      auto guard = LockGuard(fanet.x_fanet_manager_mutex);

      // Create a packet with our name "Leaf", and send it out
      FANET::NamePayload<5> namePayload;
      namePayload.name("Leaf");
      FANET::Packet<5> txPacket;
      txPacket.payload(namePayload);
      fanet.protocol->sendPacket(txPacket);

      // fanet.manager->sendPacket(namePayload, millis());
      xTaskNotify(fanet.x_fanet_tx_task, 0, eNoAction);
    }

    // Sleep for 1 second before sending the next name packet
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
    // etl::optional<Fanet::Packet> optPacket;
    etl::span<uint8_t> packetSpan{buffer.data(), length};

    if (LockGuard(x_fanet_manager_mutex)) {
      packetSpan;
      protocol->handleRx(rssi, packetSpan);
      neighbors.updateFromTable(protocol->neighborTable());
    }

    // Check if this packet should be processed by this vario and put it in the bus
    auto packet = FANET::PacketParser<FANET_MAX_FRAME_SIZE>::parse(packetSpan);
    if (!packet.payload().has_value()) {
      // If the payload is not present, we don't have a valid packet
      return;
    }

    // If the packet is unicast destined for not us, ignore it.
    if (packet.destination().has_value() &&
        packet.destination().value() != protocol->ownAddress()) {
      return;
    }

    // If the received packet is from ourself, discard it.
    if (packet.source() == protocol->ownAddress()) {
      return;
    }

    // This is a broadcast packet, or specifically destined for us.  Send it to
    // the bus for further processing.
    bus->receive(FanetPacket(packet, rssi, snr));
  }
}

void FanetRadio::taskRadioTx(void* pvParameters) {
  auto& fanetRadio = FanetRadio::getInstance();
  // auto& manager = *fanetRadio.manager;
  auto& radio = *fanetRadio.radio;

  while (true) {
    // Process sending a packet.  Sleep for the alloted time

    if (!(fanetRadio.state == FanetRadioState::RUNNING)) {
      // Wait for two seconds and try again later.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
      continue;
    }

    // Very important to take the spiLock before the radio lock here!
    // The draw methods may take out a lock on the SPI bus while they're
    // requesting information from our Fanet Manager
    uint32_t sleepMs = 0;
    if (auto spiLock = SpiLockGuard()) {
      LockGuard lock(fanetRadio.x_fanet_manager_mutex);

      // Process a TX.
      // auto currentTime = millis();
      sleepMs = fanetRadio.protocol->handleTx() - millis();

      radio.startReceive();  // Put the radio back into a receive state
    }

    // Sleep until the next due TX time.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepMs));
  }
}

bool FanetRadio::fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data) {
  // For now, just flag this as a success
  if (frameSending) {
    // If we're already sending a frame, don't send another one
    return false;
  }

  // Locks are already acquired by the taskRadioTx Task.

  // TODO:  Check if a frame is currently receiving

  // Requested coding rate
  // if (!radio->setCodingRate(codingRate)) {
  //   Serial.println("[FanetRadio] Failed to set coding rate " + String(codingRate));
  //   return false;
  // }
  // Not sure why the above is locking up.

  // Send the frame out on the wire.
  auto txResult = radio->transmit(data.data(), data.size());
  if (txResult != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Failed to transmit");
    return false;
  }

  // If we got here, the frame was sent successfully.
  Serial.println("[FanetRadio] Frame sent to lib successfully: " + String(txResult));

  // Flag the frame as sending... Not sure why this isn't being cleared by the interrupt now
  // frameSending = true;

  return txResult == RADIOLIB_ERR_NONE;
}

void FanetRadio::setupFanetHandler() {
  // Figure out what SRC address to use.
  auto addressString = getAddress();
  // Fanet::Mac srcAddress;

  // Convert the 6 character arduino hex string into 3 bytes
  uint8_t addressBytes[3];
  for (int i = 0; i < 3; i++) {
    addressBytes[i] = strtol(addressString.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }

  // Set our own address in the protocol handler.
  FANET::Address srcAddress(addressBytes[0], (addressBytes[1] << 8) | addressBytes[2]);
  protocol->ownAddress(srcAddress);
}

void FanetRadio::setup(etl::imessage_bus* bus) {
  // Sets up the radio module.  Leaves it in an uninitialized state, but
  // creates any dynamic memory required.
  this->bus = bus;
  bus->subscribe(*this);
  bus->subscribe(neighbors);  // Subscribe the neighbors to any FanetPacket updates

  // Create the FANET Protocol
  protocol = new FANET::Protocol(this);

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
#ifndef HAS_FANET
  return;  // Model does not support Fanet
#endif

  // Short circuit above taking any locks out (avoid deadlocks)
  if (region == FanetRadioRegion::OFF) {
    end();
    return;
  }

  // Initialize the random number generator
  random.initialise(millis());

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
  // 1262 radio is different from the WaveShare breadboard modules.
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
#ifndef HAS_FANET
  return;  // Model does not support Fanet
#endif

  SpiLockGuard spiLock;
  radio->sleep(false);
  state = FanetRadioState::UNINITIALIZED;
  trackingMode = etl::nullopt;  // Reset the tracking mode
}

FanetRadioState FanetRadio::getState() { return state; }

void FanetRadio::setCurrentLocation(const float& lat, const float& lon, const uint32_t& alt,
                                    const int& heading, const float& climbRate,
                                    const float& speedKmh) {
  if (state != FanetRadioState::RUNNING) {
    return;
  }

  auto ms = millis();
  if (ms < m_nextAllowedTrackingTimeMs) {
    // We're not allowed to send a tracking update yet.
    return;
  }

  // Queue the update in the TX.
  if (LockGuard(x_fanet_manager_mutex)) {
    // TODO:  Enable ground tracking modes.

    // TX the packet.
    FANET::Packet<FANET_MAX_FRAME_SIZE> trackingPacket;

    if (trackingMode.has_value()) {
      // Build a ground tracking packet for ground tracking modes
      FANET::GroundTrackingPayload groundTrackingPayload;
      groundTrackingPayload.latitude(lat)
          .longitude(lon)
          .groundType(trackingMode.value())
          .tracking(true);
      trackingPacket.payload(groundTrackingPayload);
    } else {
      // Build a Tracking packet
      FANET::TrackingPayload trackingPayload;
      trackingPayload.aircraftType(FANET::TrackingPayload::AircraftType::PARAGLIDER)
          .latitude(lat)
          .longitude(lon)
          .altitude(alt)
          .groundTrack(heading)
          .climbRate(climbRate)
          .speed(speedKmh);
      trackingPacket.payload(trackingPayload);
    }

    protocol->sendPacket(trackingPacket);
  }

  // Add a random 500ms splay to the tracking updates to ensure
  // if multiple nodes are getting GPS updates all synchronized, we don't
  // all TX at the same time.
  auto offset = random.range(75, 500);

  // Location update interval is
  // recommended interval: floor((#neighbors/10 + 1) * 5s)
  m_nextAllowedTrackingTimeMs =
      ms + offset + floor((protocol->neighborTable().size() / 10.0f + 1) + 5000);

  // Notify the Tx task that there *may* be packets to process from the manager
  xTaskNotify(x_fanet_tx_task, 0, eNoAction);
}

const FANET::Protocol::Stats FanetRadio::getStats() const {
  if (state != FanetRadioState::RUNNING) return {};

  LockGuard lock(x_fanet_manager_mutex);
  return protocol->stats();
}

const FanetNeighbors::NeighborMap& FanetRadio::getNeighborTable() const {
  LockGuard lock(x_fanet_manager_mutex);
  return neighbors.get();
}

void FanetRadio::on_receive(const GpsReading& msg) {
  // Called when a GPS reading is received from the bus.

  // Not a valid GPS location.  Bail out
  if (!msg.gps.location.isValid()) return;

  if (trackingMode.has_value() == false && flightTimer_isRunning() == false) {
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
  if (settings.fanet_address.isEmpty()) {
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
  return settings.fanet_address;
}

void FanetRadio::setGroundTrackingMode(const FANET::GroundTrackingPayload::TrackingType& mode) {
  if (state != FanetRadioState::RUNNING) {
    return;
  }
  // Acquire the manager lock, notify the manager
  LockGuard lock(x_fanet_manager_mutex);
  trackingMode =
      etl::optional<FANET::GroundTrackingPayload::TrackingType::enum_type>(mode.get_enum());
}

String FanetAddressToString(FANET::Address address) {
  char buffer[7];
  snprintf(buffer, sizeof(buffer), "%02X%02X%02X", address.manufacturer(), address.unique() << 8,
           address.unique() | 0xFF);
  buffer[6] = '\0';
  auto ret = String(buffer);
  ret.toUpperCase();
  return ret;
}