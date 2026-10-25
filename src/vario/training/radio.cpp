#include "radio.h"
#if defined(LEAF_TRAINING_STUDENT) || defined(LEAF_TRAINING_INSTRUCTOR)
#include <atomic>
#include "hardware/configuration.h"
#ifdef LEAF_TRAINING_STUDENT
#include "hardware/Leaf_SPI.h"
#endif
namespace training {
  namespace {
    std::atomic<bool> irq{false};
    void IRAM_ATTR onIrq() { irq.store(true, std::memory_order_relaxed); }
#ifdef LEAF_TRAINING_STUDENT
    using Lock = SpiLockGuard;
#else
    // Instructor owns display and radio on the same loop; there is no other SPI task.
    struct Lock {
      Lock(uint32_t, bool) {}
      explicit operator bool() const { return true; }
    };
#endif
  }  // namespace
  uint32_t deviceId() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = uint32_t(mac) ^ uint32_t(mac >> 32);
    return id ? id : 1;
  }
  bool Radio::begin() {
    if (initialized_) return true;
    Lock lock(2, false);
    if (!lock) return false;
    started_ = true;
    // Explicit bench-only US profile. Production RF operating profile remains unqualified.
    lastError = chip_.begin(920.8f, 500.0f, 7, 5, 0x12, 10, 8, 1.8f, false);
    // Match Leaf's normal radio path, including the variant-specific external RX switch.
    if (lastError == RADIOLIB_ERR_NONE) {
      chip_.setRfSwitchPins(SX1262_RF_SW, RADIOLIB_NC);
      lastError = chip_.setDio2AsRfSwitch(true);
    }
    if (lastError == RADIOLIB_ERR_NONE) lastError = chip_.setCRC(2);
    if (lastError == RADIOLIB_ERR_NONE) lastError = chip_.setRxBoostedGainMode(true, true);
    if (lastError != RADIOLIB_ERR_NONE) {
      ++errors;
      return false;
    }
    chip_.setDio1Action(onIrq);
    irq.store(false);
    initialized_ = true;
    transmitting_ = false;
    restartRx();
    return initialized_;
  }
  void Radio::restartRx() {
    irq.store(false);
    lastError = chip_.startReceive();
    if (lastError != RADIOLIB_ERR_NONE) {
      ++errors;
      initialized_ = false;
    }
  }
  bool Radio::end(bool sleep) {
    if (!started_) return true;
    Lock lock(2, false);
    if (!lock) return false;
    // Even a failed initialization may have installed/changed radio state.
    lastError = chip_.standby();
    if (lastError != RADIOLIB_ERR_NONE) return false;
    chip_.clearDio1Action();
    initialized_ = transmitting_ = false;
    irq.store(false);
    if (sleep) lastError = chip_.sleep();
    return lastError == RADIOLIB_ERR_NONE;
  }
  bool Radio::send(const Packet& packet) {
    if (!initialized_ || transmitting_) return false;
    size_t n = encode(packet, bytes_, sizeof(bytes_));
    if (!n) return false;
    Lock lock(2, false);
    if (!lock) return false;
    irq.store(false);
    lastError = chip_.startTransmit(bytes_, n);
    if (lastError != RADIOLIB_ERR_NONE) {
      ++errors;
      restartRx();
      return false;
    }
    transmitting_ = true;
    txAt_ = millis();
    ++sent;
    return true;
  }
  bool Radio::receive(Packet& packet) {
    if (!initialized_) return false;
    if (!irq.load() && (!transmitting_ || uint32_t(millis() - txAt_) < 100)) return false;
    Lock lock(2, false);
    if (!lock) return false;
    // An IRQ flag can outlive a mode transition. Check the chip's actual cause before
    // treating it as TX complete; a stale RX interrupt must not abort an active transmission.
    irq.store(false);
    const uint32_t flags = chip_.getIrqFlags();
    if (transmitting_) {
      if (!(flags & RADIOLIB_SX126X_IRQ_TX_DONE) && uint32_t(millis() - txAt_) < 100) return false;
      if (!(flags & RADIOLIB_SX126X_IRQ_TX_DONE)) ++errors;
      lastError = chip_.finishTransmit();
      if (lastError != RADIOLIB_ERR_NONE) ++errors;
      transmitting_ = false;
      restartRx();
      return false;
    }
    if (!(flags & RADIOLIB_SX126X_IRQ_RX_DONE)) {
      if (flags & (RADIOLIB_SX126X_IRQ_TIMEOUT | RADIOLIB_SX126X_IRQ_CRC_ERR |
                   RADIOLIB_SX126X_IRQ_HEADER_ERR)) {
        ++errors;
        restartRx();
      }
      return false;
    }
    size_t n = chip_.getPacketLength();
    bool ok = false;
    if (n > sizeof(bytes_) || n < 24) {
      ++errors;
    } else {
      lastError = chip_.readData(bytes_, n);
      ok = lastError == RADIOLIB_ERR_NONE && decode(bytes_, n, packet);
      if (ok)
        ++received;
      else
        ++errors;
    }
    restartRx();
    return ok;
  }
}  // namespace training
#endif
