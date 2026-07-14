#include "comms/fanet_radio.h"

#include <SPI.h>
#include "FreeRTOS.h"
#include "diagnostics/diagnostic_logs.h"
#include "diagnostics/heap_monitor.h"
#include "dispatch/message_types.h"
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

// Singleton instance declaration.
FanetRadio fanetRadio;

namespace {
  constexpr uint8_t SX1262_CMD_GET_STATUS = 0xC0;
  constexpr uint8_t SX1262_CMD_READ_REGISTER = 0x1D;
  constexpr uint8_t SX1262_CMD_SET_STANDBY = 0x80;
  constexpr uint8_t SX1262_CMD_NOP = 0x00;
  constexpr uint8_t SX1262_STANDBY_RC = 0x00;
  constexpr uint16_t SX1262_REG_VERSION_STRING = 0x0320;
  constexpr uint32_t FANET_SPI_PROBE_BUSY_TIMEOUT_MS = 100;
  constexpr uint32_t FANET_SPI_LOCK_TIMEOUT_MS = 25;
  constexpr uint32_t FANET_RADIO_SPI_CLOCK_HZ = 1000000;

  bool sx1262StatusLooksValid(uint8_t status) { return status != 0x00 && status != 0xFF; }

  bool sx126xVersionLooksValid(const char* version) {
    return strncmp(version, "SX1261", 6) == 0 || strncmp(version, "SX1262", 6) == 0;
  }

  String sx1262VersionPrefixString(const char* version) {
    String prefix;
    for (size_t i = 0; i < 6; i++) {
      const char c = version[i];
      prefix += isPrintable(c) ? c : '.';
    }
    return prefix;
  }

  String sx1262VersionHexString(const char* version) {
    char hex[18] = {0};
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X", static_cast<uint8_t>(version[0]),
             static_cast<uint8_t>(version[1]), static_cast<uint8_t>(version[2]),
             static_cast<uint8_t>(version[3]), static_cast<uint8_t>(version[4]),
             static_cast<uint8_t>(version[5]));
    return String(hex);
  }
}  // namespace

// Initial detection of Fanet module (hw3.2.6+)
bool FanetRadio::detectFanet() {
#ifdef FANET_CAPABLE
  // Auto-Detect FANET LoRa module
  pinMode(SX1262_BUSY, INPUT_PULLUP);  // chip select for the FANET module (SX1262_NSS pin)
  bool fanetReady = false;
  int count = 0;
  while (count < 25) {
    count++;
    fanetReady = !digitalRead(SX1262_BUSY);  // if busy is low, we're ready
    if (fanetReady) {
      Serial.print("Fanet check cycles: ");
      Serial.println(count);
      break;  // if we're ready, break out of the loop
    }
    delay(10);
  }
  fanetDetected_ = fanetReady;
  detectionCycles_ = count;
  return fanetReady;
#endif
  // If the module is not present, return false
  fanetDetected_ = false;
  detectionCycles_ = 0;
  return false;  // Module does not support Fanet
}

bool FanetRadio::waitFanetReady(uint32_t timeoutMs) const {
#ifdef FANET_CAPABLE
  const uint32_t startMs = millis();
  while (digitalRead(SX1262_BUSY)) {
    if (millis() - startMs >= timeoutMs) return false;
    delay(1);
  }
  return true;
#endif
  return false;
}

bool FanetRadio::probeFanetSpi() {
#if defined(FANET_CAPABLE) && defined(LORA_SX1262)
  logEvent("probe-start", "version-string");

  pinMode(SX1262_NSS, OUTPUT);
  digitalWrite(SX1262_NSS, HIGH);
  pinMode(SX1262_BUSY, INPUT_PULLUP);
  pinMode(SX1262_RESET, OUTPUT);
  digitalWrite(SX1262_RESET, LOW);
  delay(1);
  digitalWrite(SX1262_RESET, HIGH);

  if (!waitFanetReady(FANET_SPI_PROBE_BUSY_TIMEOUT_MS)) {
    state = FanetRadioState::FAILED_RADIO_PROBE;
    fanetSpiProbed_ = true;
    fanetSpiProbeOk_ = false;
    probeStatus_ = 0;
    probeDetail_ = "busy-timeout";
    logEvent("probe", "busy-timeout");
    logProbeResult();
    return false;
  }

  uint8_t firstStatus = 0;
  uint8_t secondStatus = 0;
  char version[7] = {0};
  {
    SpiLockGuard spiLock(FANET_SPI_LOCK_TIMEOUT_MS, false);
    if (!spiLock) {
      state = FanetRadioState::FAILED_RADIO_PROBE;
      fanetSpiProbed_ = true;
      fanetSpiProbeOk_ = false;
      probeStatus_ = 0;
      probeDetail_ = "spi-lock-timeout";
      logEvent("probe", "spi-lock-timeout");
      logProbeResult();
      return false;
    }

    SPI.beginTransaction(SPISettings(FANET_RADIO_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SX1262_NSS, LOW);
    firstStatus = SPI.transfer(SX1262_CMD_GET_STATUS);
    secondStatus = SPI.transfer(SX1262_CMD_NOP);
    digitalWrite(SX1262_NSS, HIGH);

    digitalWrite(SX1262_NSS, LOW);
    SPI.transfer(SX1262_CMD_SET_STANDBY);
    SPI.transfer(SX1262_STANDBY_RC);
    digitalWrite(SX1262_NSS, HIGH);
    SPI.endTransaction();

    if (!waitFanetReady(FANET_SPI_PROBE_BUSY_TIMEOUT_MS)) {
      state = FanetRadioState::FAILED_RADIO_PROBE;
      fanetSpiProbed_ = true;
      fanetSpiProbeOk_ = false;
      probeStatus_ = sx1262StatusLooksValid(firstStatus) ? firstStatus : secondStatus;
      probeDetail_ = "standby-timeout";
      logEvent("probe", "standby-timeout");
      logProbeResult();
      return false;
    }

    SPI.beginTransaction(SPISettings(FANET_RADIO_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SX1262_NSS, LOW);
    SPI.transfer(SX1262_CMD_READ_REGISTER);
    SPI.transfer(static_cast<uint8_t>((SX1262_REG_VERSION_STRING >> 8) & 0xFF));
    SPI.transfer(static_cast<uint8_t>(SX1262_REG_VERSION_STRING & 0xFF));
    SPI.transfer(SX1262_CMD_NOP);  // status/dummy byte before register data
    for (size_t i = 0; i < 6; i++) {
      version[i] = static_cast<char>(SPI.transfer(SX1262_CMD_NOP));
    }
    digitalWrite(SX1262_NSS, HIGH);
    SPI.endTransaction();
  }

  const uint8_t status = sx1262StatusLooksValid(firstStatus) ? firstStatus : secondStatus;
  const String versionPrefix = sx1262VersionPrefixString(version);
  fanetSpiProbed_ = true;
  fanetSpiProbeOk_ = sx126xVersionLooksValid(version);
  probeStatus_ = status;
  probeDetail_ = fanetSpiProbeOk_ ? String("ok:") + versionPrefix
                                  : String("version:") + versionPrefix +
                                        ":hex:" + sx1262VersionHexString(version);
  logEvent("probe-status", sx1262StatusLooksValid(status) ? "ok" : "invalid", "status", status,
           true);
  logEvent("probe-version", fanetSpiProbeOk_ ? String("ok:") + versionPrefix : versionPrefix);
  logEvent("probe-version-hex", sx1262VersionHexString(version));
  logProbeResult();

  if (!fanetSpiProbeOk_) {
    state = FanetRadioState::FAILED_RADIO_PROBE;
    return false;
  }

  return true;
#endif
  return false;
}

void FanetRadio::logDetectionResult() {
  if (!detectionResultLogged_ &&
      diagnostic_logs::appendSystemEvent("fanet", "detect", fanetDetected_ ? "present" : "missing",
                                         "cycles", detectionCycles_, true)) {
    detectionResultLogged_ = true;
  }
  logProbeResult();
}

void FanetRadio::logProbeResult() {
  if (probeResultLogged_ || !fanetSpiProbed_) return;
  String detail = probeDetail_.isEmpty() ? (fanetSpiProbeOk_ ? "ok" : "failed") : probeDetail_;
  if (diagnostic_logs::appendSystemEvent("fanet", "probe", detail, "status", probeStatus_, true)) {
    probeResultLogged_ = true;
  }
}

void FanetRadio::logEvent(const char* event, const String& detail, const char* key, int32_t value,
                          bool hasValue) {
  logDetectionResult();
  if (state == FanetRadioState::UNINSTALLED) return;
  diagnostic_logs::appendSystemEvent("fanet", event, detail, key, value, hasValue);
}

void FanetRadio::logEventCode(const char* event, const String& detail, const int16_t code) {
  logEvent(event, detail, "code", code, true);
}

bool FanetRadio::radioUnavailable() const {
  return state == FanetRadioState::UNINSTALLED || state == FanetRadioState::FAILED_RADIO_PROBE ||
         state == FanetRadioState::FAILED_RADIO_INIT || state == FanetRadioState::FAILED_OTHER;
}

bool FanetRadio::shouldMarkRadioUnhealthy(int16_t code) const {
  switch (code) {
    case RADIOLIB_ERR_NONE:
      return false;
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
    case RADIOLIB_ERR_SPI_WRITE_FAILED:
    case RADIOLIB_ERR_WRONG_MODEM:
    case RADIOLIB_ERR_SPI_CMD_TIMEOUT:
      return true;
    default:
      return false;
  }
}

void FanetRadio::markRadioUnhealthy(const char* event, int16_t code) {
  if (state == FanetRadioState::UNINSTALLED || state == FanetRadioState::FAILED_RADIO_PROBE) {
    return;
  }
  state = FanetRadioState::FAILED_RADIO_INIT;
  logEvent("radio-unhealthy", event, "code", code, true);
}

// Static initializers
volatile bool FanetRadio::frameSending = false;

ICACHE_RAM_ATTR void FanetRadio::onRxIsr() {
  auto& fanet = fanetRadio;
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
  auto& fanet = fanetRadio;

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
  auto& fanet = fanetRadio;

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
  if (state != FanetRadioState::RUNNING) return;

  auto& radio = *FanetRadio::radio;

  int16_t rxState = RADIOLIB_ERR_UNKNOWN;
  size_t length;
  if (auto lockGuard = SpiLockGuard(FANET_SPI_LOCK_TIMEOUT_MS, false)) {
    length = radio.getPacketLength();

    // If a 0 length packet is here, I'm guessing the "done" DIO pin was fired
    // without there being any data to process.  Just clear this and continue on
    // our way for the next notification.
    if (length == 0) {
      return;
    }

    // A packet is able to be read
    rxState =
        callRadio("rx-read", "readData", [&]() { return radio.readData(buffer.data(), length); });
  } else {
    logEvent("rx-read", "spi-lock-timeout");
    return;
  }

  if (rxState != RADIOLIB_ERR_NONE) {
    return;
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
    bus_->receive(FanetPacket(packet, rssi, snr));
  }
}

void FanetRadio::taskRadioTx(void* pvParameters) {
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
    if (auto spiLock = SpiLockGuard(FANET_SPI_LOCK_TIMEOUT_MS, false)) {
      LockGuard lock(fanetRadio.x_fanet_manager_mutex);
      if (!lock) {
        fanetRadio.logEvent("tx-manager-lock", "failed");
      } else {
        // Process a TX.
        // auto currentTime = millis();
        sleepMs = fanetRadio.protocol->handleTx() - millis();

        fanetRadio.logEvent("tx-rx-restart-start", "startReceive");
        fanetRadio.callRadio("tx-rx-restart", "startReceive", [&]() {
          return radio.startReceive();  // Put the radio back into a receive state
        });
      }
    } else {
      fanetRadio.logEvent("tx-rx-restart", "spi-lock-timeout");
    }

    // Sleep until the next due TX time.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepMs));
  }
}

bool FanetRadio::fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data) {
  if (state != FanetRadioState::RUNNING) {
    return false;
  }

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
  logEvent("tx-start", "transmit", "bytes", data.size(), true);
  auto txResult = callRadio("tx-result", "transmit",
                            [&]() { return radio->transmit(data.data(), data.size()); });
  if (txResult != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Failed to transmit");
    return false;
  }

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

void FanetRadio::subscribe(etl::imessage_bus* bus) {
  if (!bus->subscribe(*this)) {
    fatalError("FanetRadio couldn't subscribe to message bus");
  };

  // Subscribe the neighbors to any FanetPacket updates
  if (!bus->subscribe(neighbors)) {
    fatalError("FanetRadio couldn't subscribe neighbors to message bus");
  }
}

void FanetRadio::setup() {
  logDetectionResult();
  heap_monitor::checkpoint("fanet-setup-start");
  if (state == FanetRadioState::UNINSTALLED) {
    heap_monitor::checkpoint("fanet-setup-skipped");
    return;
  }

  if (!probeFanetSpi()) {
    heap_monitor::checkpoint("fanet-setup-probe-failed");
    return;
  }

  // Sets up the radio module.  Leaves it in an uninitialized state, but
  // creates any dynamic memory required.

  // Create the FANET Protocol
  protocol = new FANET::Protocol(this);
  heap_monitor::checkpoint("fanet-protocol");

  // Create the mutex, lock it.
  x_fanet_manager_mutex = xSemaphoreCreateMutex();
  if (x_fanet_manager_mutex == NULL) {
    state = FanetRadioState::FAILED_OTHER;
    logEvent("setup", "mutex-failed");
    return;
  }

#ifdef LORA_SX1262
  radio = new SX1262(&radioModule);
  heap_monitor::checkpoint("fanet-radio-alloc");

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
    logEvent("setup-tx-task", "failed", "code", taskCreateCode, true);
    return;
  }
  heap_monitor::registerTask("fanet_tx", x_fanet_tx_task);
  heap_monitor::checkpoint("fanet-tx-task");

  // Create the RX Task
  taskCreateCode = xTaskCreate(taskRadioRx, "FanetRx", 4096, nullptr,
                               1,  // Typical lower priority task
                               &x_fanet_rx_task);
  if (taskCreateCode != pdPASS) {
    Serial.print((String) "Creating Rx task failed: " + taskCreateCode);
    state = FanetRadioState::FAILED_OTHER;
    logEvent("setup-rx-task", "failed", "code", taskCreateCode, true);
    return;
  }
  heap_monitor::registerTask("fanet_rx", x_fanet_rx_task);
  heap_monitor::checkpoint("fanet-rx-task");

  // TODO:  Add a setting for sending out periodic Fanet names
  // Create the name TX task
  // Just disable this for now until we support names :)
  // taskCreateCode =
  //     xTaskCreate(taskRadioNameTx, "FanetNameTx", 4096, nullptr, 1, &x_fanet_tx_name_task);

  // Put the radio to sleep once it has been initialized.
  logEvent("setup-sleep-start", "sleep");
  if (auto spiLock = SpiLockGuard(FANET_SPI_LOCK_TIMEOUT_MS, false)) {
    callRadio("setup-sleep", "sleep", [&]() { return radio->sleep(); });
  } else {
    state = FanetRadioState::FAILED_RADIO_INIT;
    logEvent("setup-sleep", "spi-lock-timeout");
  }
  heap_monitor::checkpoint("fanet-setup-end");
}

void FanetRadio::begin(const FanetRadioRegion& region) {
#ifndef FANET_CAPABLE
  return;  // Model does not support Fanet
#endif

  logDetectionResult();
  if (state == FanetRadioState::UNINSTALLED) {
    return;  // Short circuit if the radio module is not installed
  }

  // Short circuit above taking any locks out (avoid deadlocks)
  if (region == FanetRadioRegion::OFF) {
    logEvent("begin", "region-off");
    end();
    return;
  }

  if (radioUnavailable()) {
    logEvent("begin", "radio-unavailable");
    return;
  }

  if (radio == nullptr || x_fanet_manager_mutex == nullptr) {
    state = FanetRadioState::FAILED_OTHER;
    logEvent("begin", "not-setup");
    return;
  }

  // Initialize the random number generator
  random.initialise(millis());

  heap_monitor::checkpoint("fanet-begin-start");
  logEvent("begin-start", region.c_str());
  state = FanetRadioState::INITIALIZING;
  int16_t radioInitState = RADIOLIB_ERR_UNKNOWN;
  int16_t syncWordState = RADIOLIB_ERR_UNKNOWN;
  // Always take the SPI lock out before the Fanet Manager lock
  // to avoid deadlocks with janky display modules locking the SPI
  // bus and making Fanet state changes or requests after.
  SpiLockGuard spiLock(FANET_SPI_LOCK_TIMEOUT_MS, false);
  if (!spiLock) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    logEvent("begin", "spi-lock-timeout");
    return;
  }
  LockGuard lock(x_fanet_manager_mutex);
  if (!lock) {
    state = FanetRadioState::FAILED_OTHER;
    logEvent("begin", "manager-lock-timeout");
    return;
  }

  Serial.println("[FanetRadio] Initializing");

  // Initialize the radio for the settings of the given region
  // A note on SYNC-WORD:
  // The setSyncWord 0xF1, 0x44 ends up doing some bit shifting
  // https://github.com/lyusupov/SoftRF/blob/00209ce2eb4447ea4404901dc0b9c102f736ca87/software/firmware/source/libraries/RadioLib/src/modules/SX126x/SX126x.cpp#L944C55-L953
  // and ends up setting 0xF4 0x14 into the registers.
  // This is the value as specified in the FANET spec for SX1262 chips.
  // We got these values too from OGN-Tracker

  switch (region) {
    case FanetRadioRegion::US:
      logEvent("begin-radio-start", "US");
      radioInitState = callRadio("begin-radio", "US", [&]() {
        return radio->begin(920.800f, 500.0f, 7U, 5U, 0xF1, 22U, 8U, 1.8f, false);
      });
      if (radioInitState == RADIOLIB_ERR_NONE) {
        syncWordState =
            callRadio("begin-sync-word", "US", [&]() { return radio->setSyncWord(0xF1, 0x44); });
      }
      break;
    case FanetRadioRegion::EUROPE:
      logEvent("begin-radio-start", "EUROPE");
      radioInitState = callRadio("begin-radio", "EUROPE", [&]() {
        return radio->begin(868.200f, 250.0f, 7U, 5U, 0xF1, 22U, 8U, 1.8f, false);
      });
      if (radioInitState == RADIOLIB_ERR_NONE) {
        syncWordState = callRadio("begin-sync-word", "EUROPE",
                                  [&]() { return radio->setSyncWord(0xF1, 0x44); });
      }
      break;
  }

  if (radioInitState != RADIOLIB_ERR_NONE) {
    Serial.printf("[FanetRadio] Module initialization failed: %d\n", radioInitState);
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }
  if (syncWordState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

  Serial.println("[FanetRadio] Initialized");

  logEvent("begin-start-rx-start", "startReceive");
  auto rxState =
      callRadio("begin-start-rx", "startReceive", [&]() { return radio->startReceive(); });
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Radio->startReceive failed");
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }

#ifdef LORA_SX1262
  // 1262 radio is different from the WaveShare breadboard modules.
  radio->setRfSwitchPins(SX1262_RF_SW, RADIOLIB_NC);
  logEvent("begin-dio2-rf-switch-start", "setDio2AsRfSwitch");
  auto dio2State = callRadio("begin-dio2-rf-switch", "setDio2AsRfSwitch",
                             [&]() { return radio->setDio2AsRfSwitch(); });
  if (dio2State != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }
  // Try to get a few extra db
  logEvent("begin-rx-boost-start", "setRxBoostedGainMode");
  auto boostedGainState = callRadio("begin-rx-boost", "setRxBoostedGainMode",
                                    [&]() { return radio->setRxBoostedGainMode(true, true); });
  if (boostedGainState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return;
  }
#endif

  // We're finished, release the locks.
  state = FanetRadioState::RUNNING;
  logEvent("begin-end", "running");
  heap_monitor::checkpoint("fanet-begin-end");
}

void FanetRadio::end() {
#ifndef FANET_CAPABLE
  return;  // Model does not support Fanet
#endif

  logDetectionResult();
  // Short circuit unloading if the radio module is missing.
  if (state == FanetRadioState::UNINSTALLED || state == FanetRadioState::FAILED_RADIO_PROBE ||
      state == FanetRadioState::FAILED_RADIO_INIT || state == FanetRadioState::FAILED_OTHER ||
      radio == nullptr) {
    logEvent("end", "radio-unavailable");
    return;
  }

  SpiLockGuard spiLock(FANET_SPI_LOCK_TIMEOUT_MS, false);
  if (!spiLock) {
    logEvent("end", "spi-lock-timeout");
    return;
  }
  heap_monitor::checkpoint("fanet-end-start");
  logEvent("end-sleep-start", "sleep");
  callRadio("end-sleep", "sleep", [&]() { return radio->sleep(false); });
  state = FanetRadioState::UNINITIALIZED;
  trackingMode = etl::nullopt;  // Reset the tracking mode
  logEvent("end", "uninitialized");
  heap_monitor::checkpoint("fanet-end-end");
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
    trackingPacket.forward(settings.dev_fanetFwd);

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
          .tracking(true)
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
  if (!lock) return {};
  return protocol->stats();
}

const FanetNeighbors::NeighborMap& FanetRadio::getNeighborTable() const {
  if (state != FanetRadioState::RUNNING || x_fanet_manager_mutex == nullptr) {
    return neighbors.get();
  }
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
  float climbRate = 0;
  if (baro.climbRateFilteredValid()) {
    climbRate = baro.climbRateFiltered() / 100.0f;
  }
  setCurrentLocation(gps.location.lat(), gps.location.lng(), gps.altitude.meters(),
                     gps.course.deg(), climbRate, gps.speed.kmph());
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
    settings.fanet_address = ret;  // save newly captured address
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
