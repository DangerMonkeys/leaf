#include "comms/fanet_radio.h"

#include <SPI.h>
#include <new>
#ifdef LORA_SX1262
#include <hal/Arduino/ArduinoHal.h>
#endif
#include "FreeRTOS.h"
#include "diagnostics/diagnostic_logs.h"
#include "diagnostics/heap_monitor.h"
#include "dispatch/message_types.h"
#include "esp_mac.h"
#include "etl/array.h"
#include "fanet/packetParser.hpp"
#include "fanet/tracking.hpp"
#include "hardware/Leaf_SPI.h"
#include "hardware/configuration.h"
#include "instruments/baro.h"
#include "logging/log.h"
#include "utils/lock_guard.h"

// Singleton instance declaration.
FanetRadio fanetRadio;

struct FanetRadio::RadioHardware {
#ifdef LORA_SX1262
  ArduinoHal hal{SPI};
  Module module{&hal, SX1262_NSS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY};
  SX1262 radio{&module};
#endif
};

namespace {
  portMUX_TYPE rxInterruptLock = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t rxInterruptTask = nullptr;
  constexpr uint32_t FANET_STOP_TIMEOUT_MS = 1500;

  void setRxInterruptTask(TaskHandle_t task) {
    portENTER_CRITICAL(&rxInterruptLock);
    rxInterruptTask = task;
    portEXIT_CRITICAL(&rxInterruptLock);
  }

  void parkWorker(std::atomic<bool>& parked) {
    parked.store(true);
    for (;;) vTaskSuspend(nullptr);
  }

  constexpr uint8_t SX1262_CMD_GET_STATUS = 0xC0;
  constexpr uint8_t SX1262_CMD_READ_REGISTER = 0x1D;
  constexpr uint8_t SX1262_CMD_SET_STANDBY = 0x80;
  constexpr uint8_t SX1262_CMD_NOP = 0x00;
  constexpr uint8_t SX1262_STANDBY_RC = 0x00;
  constexpr uint16_t SX1262_REG_VERSION_STRING = 0x0320;
  constexpr uint32_t FANET_SPI_PROBE_BUSY_TIMEOUT_MS = 100;
  constexpr uint32_t FANET_SPI_LOCK_TIMEOUT_MS = 25;
  constexpr uint32_t FANET_RADIO_SPI_CLOCK_HZ = 1000000;
  constexpr int8_t FANET_TX_POWER_DBM = 22;
  constexpr float SX1262_TX_CURRENT_LIMIT_MA = 140.0f;

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

    // The probe wakes the chip. Put a recognized radio back to sleep without
    // constructing the runtime driver or starting its workers.
    if (sx126xVersionLooksValid(version)) {
      if (!waitFanetReady(FANET_SPI_PROBE_BUSY_TIMEOUT_MS)) {
        state = FanetRadioState::FAILED_RADIO_INIT;
        logEvent("setup-sleep", "busy-timeout");
      } else {
        SPI.beginTransaction(SPISettings(FANET_RADIO_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
        digitalWrite(SX1262_NSS, LOW);
        SPI.transfer(RADIOLIB_SX126X_CMD_SET_SLEEP);
        SPI.transfer(RADIOLIB_SX126X_SLEEP_START_WARM);
        digitalWrite(SX1262_NSS, HIGH);
        SPI.endTransaction();
        delay(1);
      }
    }
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
         state == FanetRadioState::FAILED_RADIO_INIT;
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

ICACHE_RAM_ATTR void FanetRadio::onRxIsr() {
  BaseType_t shouldYield = pdFALSE;
  portENTER_CRITICAL_ISR(&rxInterruptLock);
  if (rxInterruptTask) vTaskNotifyGiveFromISR(rxInterruptTask, &shouldYield);
  portEXIT_CRITICAL_ISR(&rxInterruptLock);
  portYIELD_FROM_ISR(shouldYield);
}

void FanetRadio::taskRadioRx(void* pvParameters) {
  auto& fanet = fanetRadio;
  while (!fanet.producers_.stopping()) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (fanet.state == FanetRadioState::RUNNING && !fanet.producers_.stopping()) {
      fanet.processRxPacket();
      xTaskNotifyGive(fanet.x_fanet_tx_task);
    }
  }
  parkWorker(fanet.rxParked_);
}

void FanetRadio::processRxPacket() {
  if (state != FanetRadioState::RUNNING) return;

  auto& radio = *FanetRadio::radio;

  int16_t rxState = RADIOLIB_ERR_UNKNOWN;
  size_t length;
  float rssi = 0;
  float snr = 0;
  if (auto lockGuard = SpiLockGuard(FANET_SPI_LOCK_TIMEOUT_MS, false)) {
    length = radio.getPacketLength();

    // If a 0 length packet is here, I'm guessing the "done" DIO pin was fired
    // without there being any data to process.  Just clear this and continue on
    // our way for the next notification.
    if (length == 0 || length > buffer.size()) {
      return;
    }

    // A packet is able to be read
    rxState =
        callRadio("rx-read", "readData", [&]() { return radio.readData(buffer.data(), length); });
    if (rxState == RADIOLIB_ERR_NONE) {
      rssi = radio.getRSSI();
      snr = radio.getSNR();
    }
  } else {
    logEvent("rx-read", "spi-lock-timeout");
    return;
  }

  if (rxState != RADIOLIB_ERR_NONE) {
    return;
  }

  if (rxState == RADIOLIB_ERR_NONE) {
    // A packet was received successfully.  Process it in our Fanet manager
    // etl::optional<Fanet::Packet> optPacket;
    etl::span<uint8_t> packetSpan{buffer.data(), length};

    if (LockGuard(x_fanet_manager_mutex, pdMS_TO_TICKS(FANET_SPI_LOCK_TIMEOUT_MS), false)) {
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
  auto& radio = *fanetRadio.radio;

  while (!fanetRadio.producers_.stopping()) {
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
      LockGuard lock(fanetRadio.x_fanet_manager_mutex, pdMS_TO_TICKS(FANET_SPI_LOCK_TIMEOUT_MS),
                     false);
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
  parkWorker(fanetRadio.txParked_);
}

bool FanetRadio::fanet_sendFrame(uint8_t codingRate, etl::span<const uint8_t> data) {
  if (!isDetected() || state != FanetRadioState::RUNNING) {
    return false;
  }

  // Locks are already acquired by the taskRadioTx Task.

  // TODO:  Check if a frame is currently receiving

  // FANET requests 4/8 below eight neighbors, otherwise 4/5. Change modulation only in
  // standby; taskRadioTx holds both locks and restores reception even if this send fails.
  if (codingRate != appliedCodingRate_) {
    auto standbyState =
        callRadio("tx-coding-standby", "standby", [&]() { return radio->standby(); });
    if (standbyState != RADIOLIB_ERR_NONE) return false;

    // RadioLib updates its cached modulation parameters before the SPI write succeeds.
    // Invalidate our cache so a failed write is retried, including a return to the old rate.
    appliedCodingRate_ = 0;
    auto codingState = callRadio("tx-coding-rate", "setCodingRate",
                                 [&]() { return radio->setCodingRate(codingRate); });
    if (codingState != RADIOLIB_ERR_NONE) return false;
    appliedCodingRate_ = codingRate;
    logEvent("tx-coding-rate-applied", "4/" + String(codingRate), "denominator", codingRate, true);
  }

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
  heap_monitor::checkpoint("fanet-setup-start");
  if (!detectFanet()) {
    state = FanetRadioState::UNINSTALLED;
    logDetectionResult();
    heap_monitor::checkpoint("fanet-setup-skipped");
    return;
  }

  state = FanetRadioState::UNINITIALIZED;
  logDetectionResult();

  if (probeFanetSpi()) {
    heap_monitor::checkpoint("fanet-setup-end");
  } else {
    heap_monitor::checkpoint("fanet-setup-probe-failed");
  }
}

bool FanetRadio::allocateResources() {
#ifdef LORA_SX1262
  hardware_ = new (std::nothrow) RadioHardware;
  if (!hardware_) return false;
  radio = &hardware_->radio;
  protocol = new (std::nothrow) FANET::Protocol(this);
  if (!protocol) return false;
  x_fanet_manager_mutex = xSemaphoreCreateMutex();
  if (!x_fanet_manager_mutex) return false;
  setupFanetHandler();

  rxParked_.store(false);
  txParked_.store(false);
  producers_.reopen();
  if (xTaskCreate(taskRadioTx, "FanetTx", 4096, nullptr, 1, &x_fanet_tx_task) != pdPASS)
    return false;
  heap_monitor::registerTask("fanet_tx", x_fanet_tx_task);
  if (xTaskCreate(taskRadioRx, "FanetRx", 4096, nullptr, 1, &x_fanet_rx_task) != pdPASS)
    return false;
  heap_monitor::registerTask("fanet_rx", x_fanet_rx_task);

  setRxInterruptTask(x_fanet_rx_task);
  radio->setPacketReceivedAction(onRxIsr);
  irqAttached_ = true;
  return true;
#else
  return false;
#endif
}

void FanetRadio::begin(const FanetRadioRegion& region) {
  if (!isDetected()) return;
  if (region == FanetRadioRegion::OFF) {
    end();
    return;
  }
  if (webserver_user_app_active()) return;
  if (radioUnavailable()) {
    logEvent("begin", "radio-unavailable");
    return;
  }
  if (hardware_) {
    end();
    if (hardware_ || radioUnavailable()) return;
  }

  state = FanetRadioState::INITIALIZING;
  if (!allocateResources()) {
    state = FanetRadioState::FAILED_OTHER;
    logEvent("begin", "resource-allocation-failed");
    releaseResources();
    return;
  }
  if (!initializeRadio(region)) {
    releaseResources();
    return;
  }

  state = FanetRadioState::RUNNING;
  xTaskNotifyGive(x_fanet_tx_task);
  logEvent("begin-end", "running");
  heap_monitor::checkpoint("fanet-begin-end");
}

bool FanetRadio::initializeRadio(const FanetRadioRegion& region) {
  // Initialize the random number generator
  random.initialise(millis());

  heap_monitor::checkpoint("fanet-begin-start");
  logEvent("begin-start", region.c_str());
  int16_t radioInitState = RADIOLIB_ERR_UNKNOWN;
  int16_t syncWordState = RADIOLIB_ERR_UNKNOWN;
  // Always take the SPI lock out before the Fanet Manager lock
  // to avoid deadlocks with janky display modules locking the SPI
  // bus and making Fanet state changes or requests after.
  SpiLockGuard spiLock(FANET_SPI_LOCK_TIMEOUT_MS, false);
  if (!spiLock) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    logEvent("begin", "spi-lock-timeout");
    return false;
  }
  LockGuard lock(x_fanet_manager_mutex);
  if (!lock) {
    state = FanetRadioState::FAILED_OTHER;
    logEvent("begin", "manager-lock-timeout");
    return false;
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
        return radio->begin(920.800f, 500.0f, 7U, 5U, 0xF1, FANET_TX_POWER_DBM, 8U, 1.8f, false);
      });
      if (radioInitState == RADIOLIB_ERR_NONE) {
        syncWordState =
            callRadio("begin-sync-word", "US", [&]() { return radio->setSyncWord(0xF1, 0x44); });
      }
      break;
    case FanetRadioRegion::EUROPE:
      logEvent("begin-radio-start", "EUROPE");
      radioInitState = callRadio("begin-radio", "EUROPE", [&]() {
        return radio->begin(868.200f, 250.0f, 7U, 5U, 0xF1, FANET_TX_POWER_DBM, 8U, 1.8f, false);
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
    return false;
  }
  if (syncWordState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return false;
  }

  appliedCodingRate_ = 5;  // begin() resets the radio to 4/5 on every region change/restart.
  logEvent("begin-tx-power", "requested", "dBm", FANET_TX_POWER_DBM, true);

  // RadioLib 7.1.2 initializes OCP to 60 mA and preserves it during PA setup. The SX1262
  // needs its 140 mA limit for the requested +22 dBm; set it after begin() configures the PA.
  auto currentLimitState = callRadio("begin-current-limit", "setCurrentLimit", [&]() {
    return radio->setCurrentLimit(SX1262_TX_CURRENT_LIMIT_MA);
  });
  if (currentLimitState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return false;
  }
  const float currentLimitMa = radio->getCurrentLimit();
  logEvent("begin-current-limit-readback", "getCurrentLimit", "mA",
           static_cast<int32_t>(currentLimitMa), true);
  if (currentLimitMa != SX1262_TX_CURRENT_LIMIT_MA) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    logEvent("begin-current-limit", "readback-mismatch");
    return false;
  }

  Serial.println("[FanetRadio] Initialized");

  logEvent("begin-start-rx-start", "startReceive");
  auto rxState =
      callRadio("begin-start-rx", "startReceive", [&]() { return radio->startReceive(); });
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.println("[FanetRadio] Radio->startReceive failed");
    state = FanetRadioState::FAILED_RADIO_INIT;
    return false;
  }

#ifdef LORA_SX1262
  // 1262 radio is different from the WaveShare breadboard modules.
  radio->setRfSwitchPins(SX1262_RF_SW, RADIOLIB_NC);
  logEvent("begin-dio2-rf-switch-start", "setDio2AsRfSwitch");
  auto dio2State = callRadio("begin-dio2-rf-switch", "setDio2AsRfSwitch",
                             [&]() { return radio->setDio2AsRfSwitch(); });
  if (dio2State != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return false;
  }
  // Try to get a few extra db
  logEvent("begin-rx-boost-start", "setRxBoostedGainMode");
  auto boostedGainState = callRadio("begin-rx-boost", "setRxBoostedGainMode",
                                    [&]() { return radio->setRxBoostedGainMode(true, true); });
  if (boostedGainState != RADIOLIB_ERR_NONE) {
    state = FanetRadioState::FAILED_RADIO_INIT;
    return false;
  }
#endif

  return true;
}

bool FanetRadio::releaseResources() {
  if (!hardware_) return true;

  producers_.requestStop();
  setRxInterruptTask(nullptr);
  if (x_fanet_rx_task) xTaskNotifyGive(x_fanet_rx_task);
  if (x_fanet_tx_task) xTaskNotifyGive(x_fanet_tx_task);

  const uint32_t started = millis();
  while (!producers_.idle() || (x_fanet_rx_task && !rxParked_.load()) ||
         (x_fanet_tx_task && !txParked_.load())) {
    if (millis() - started >= FANET_STOP_TIMEOUT_MS) {
      logEvent("end", "worker-timeout");
      return false;
    }
    delay(1);
  }

  // A task parked outside the protocol/SPI locks cannot touch the shared objects.
  // On a timeout, leave ownership intact so a later stop can safely finish.
  if (irqAttached_) {
    SpiLockGuard spiLock(FANET_SPI_LOCK_TIMEOUT_MS, false);
    if (!spiLock) {
      logEvent("end", "spi-lock-timeout");
      return false;
    }
    radio->clearPacketReceivedAction();
    irqAttached_ = false;
    callRadio("end-sleep", "sleep", [&]() { return radio->sleep(false); });
  }
  if (x_fanet_rx_task) {
    heap_monitor::registerTask("fanet_rx", nullptr);
    vTaskDelete(x_fanet_rx_task);
    x_fanet_rx_task = nullptr;
  }
  if (x_fanet_tx_task) {
    heap_monitor::registerTask("fanet_tx", nullptr);
    vTaskDelete(x_fanet_tx_task);
    x_fanet_tx_task = nullptr;
  }
  if (x_fanet_manager_mutex) {
    vSemaphoreDelete(x_fanet_manager_mutex);
    x_fanet_manager_mutex = nullptr;
  }
  delete protocol;
  protocol = nullptr;
  delete hardware_;
  hardware_ = nullptr;
  radio = nullptr;
  return true;
}

void FanetRadio::end() {
  if (!hardware_) return;
  heap_monitor::checkpoint("fanet-end-start");
  if (state == FanetRadioState::RUNNING) state = FanetRadioState::UNINITIALIZED;
  trackingMode.store(NO_GROUND_TRACKING);
  if (releaseResources()) {
    logEvent("end", "released");
    heap_monitor::checkpoint("fanet-end-end");
  }
}

FanetRadioState FanetRadio::getState() { return state.load(); }

void FanetRadio::setCurrentLocation(const float& lat, const float& lon, const uint32_t& alt,
                                    const int& heading, const float& climbRate,
                                    const float& speedKmh) {
  if (!isDetected() || state != FanetRadioState::RUNNING) {
    return;
  }

  ProducerGate::Entry entry(producers_);
  if (!entry || state != FanetRadioState::RUNNING) return;

  LockGuard lock(x_fanet_manager_mutex, pdMS_TO_TICKS(FANET_SPI_LOCK_TIMEOUT_MS), false);
  if (!lock) return;

  auto ms = millis();
  if (ms < m_nextAllowedTrackingTimeMs) {
    // We're not allowed to send a tracking update yet.
    return;
  }

  // Queue the update in the TX.
  FANET::Packet<FANET_MAX_FRAME_SIZE> trackingPacket;
  trackingPacket.forward(settings.dev_fanetFwd);

  const int mode = trackingMode.load();
  if (mode != NO_GROUND_TRACKING) {
    // Build a ground tracking packet for ground tracking modes
    FANET::GroundTrackingPayload groundTrackingPayload;
    groundTrackingPayload.latitude(lat)
        .longitude(lon)
        .groundType(static_cast<FANET::GroundTrackingPayload::TrackingType::enum_type>(mode))
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

  // Add a random 500ms splay to the tracking updates to ensure
  // if multiple nodes are getting GPS updates all synchronized, we don't
  // all TX at the same time.
  auto offset = random.range(75, 500);

  // Location update interval is
  // recommended interval: floor((#neighbors/10 + 1) * 5s)
  m_nextAllowedTrackingTimeMs =
      ms + offset + floor((protocol->neighborTable().size() / 10.0f + 1) + 5000);

  // Notify the Tx task that there *may* be packets to process from the manager
  xTaskNotifyGive(x_fanet_tx_task);
}

const FANET::Protocol::Stats FanetRadio::getStats() const {
  if (!isDetected() || state != FanetRadioState::RUNNING) return {};

  ProducerGate::Entry entry(producers_);
  if (!entry || state != FanetRadioState::RUNNING) return {};
  LockGuard lock(x_fanet_manager_mutex, pdMS_TO_TICKS(FANET_SPI_LOCK_TIMEOUT_MS), false);
  if (!lock) return {};
  return protocol->stats();
}

const FanetNeighbors::NeighborMap& FanetRadio::getNeighborTable() const { return neighbors.get(); }

void FanetRadio::on_receive(const GpsReading& msg) {
  // Called when a GPS reading is received from the bus.

  // Not a valid GPS location.  Bail out
  if (!msg.gps.location.isValid()) return;

  if (trackingMode.load() == NO_GROUND_TRACKING && !flightTimer_isRunning()) {
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
  if (!isDetected() || state != FanetRadioState::RUNNING) {
    return;
  }
  trackingMode.store(mode.get_enum());
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
