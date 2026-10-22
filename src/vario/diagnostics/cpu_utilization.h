#pragma once

#include <Arduino.h>
#include <atomic>

#include "diagnostics/gps_profile.h"
#include "diagnostics/runtime_capture.h"
#include "ui/input/buttons.h"

namespace cpu_utilization {
  // Capture control and recorder mutations belong to the firmware loop thread.
  // capturing() is also safe for diagnostic writers on background tasks.

  bool enabled();
  extern std::atomic<bool> captureActive;
  inline bool capturing() { return captureActive.load(std::memory_order_relaxed); }
  bool hasCapture();
  const char* status();
  uint64_t nowUs();
  uint64_t workClockUs();
  void update();
  bool start();
  void freeze();
  bool exportToSd();
  const char* exportedPath();
  bool writeJson(Print& output);
  void beginSlot(uint8_t index, uint32_t tick, uint64_t expectedUs, uint64_t actualUs);
  void tasksFinished(uint8_t buttons, uint8_t displayContext);
  void baroStarting();
  void timerArmed(uint32_t uncertaintyUs);
  void recordButtonEvent(Button button, ButtonEvent event);
  void recordDisplay(uint8_t context, uint64_t startWorkUs);
  uint8_t buttonPinMask(Button button);
  void addWork(runtime_capture::Work kind, uint64_t startWorkUs, bool didWork = true);
  bool gpsBegin(const char* sentence, unsigned length, bool injected);
  void gpsEnd();
  bool gpsUartBegin(bool prefixMissing);
  void gpsUartEnd(bool payload);
  void gpsUartDiscard();
  void gpsValidity(bool accepted);
  uint8_t gpsStageEnter(runtime_capture::GpsStage stage);
  void gpsStageExit(uint8_t previous);

  class GpsSentenceScope {
   public:
    GpsSentenceScope(const char* text, unsigned length, bool injected = false)
        : active_(capturing() && gpsBegin(text, length, injected)) {}
    ~GpsSentenceScope() {
      if (active_) gpsEnd();
    }

   private:
    bool active_;
  };
  class GpsStageScope {
   public:
    explicit GpsStageScope(runtime_capture::GpsStage stage)
        : previous_(capturing() ? gpsStageEnter(stage) : 255) {}
    ~GpsStageScope() {
      if (previous_ != 255) gpsStageExit(previous_);
    }

   private:
    uint8_t previous_;
  };
  class GpsUartScope {
   public:
    GpsUartScope(bool prefixMissing, bool measure)
        : active_(measure && gpsUartBegin(prefixMissing)) {}
    void finish(bool payload) {
      if (active_) {
        gpsUartEnd(payload);
        active_ = false;
      }
    }
    void discard() {
      if (!active_) return;
      finish(true);
      gpsUartDiscard();
      active_ = gpsUartBegin(true);
    }
    ~GpsUartScope() { finish(false); }

   private:
    bool active_;
  };

  class Scope {
   public:
    explicit Scope(runtime_capture::Work kind, bool didWork = true, bool measure = true)
        : kind_(kind),
          didWork_(didWork),
          active_(measure && capturing()),
          start_(active_ ? workClockUs() : 0) {}
    ~Scope() {
      if (active_) addWork(kind_, start_, didWork_);
    }

   private:
    runtime_capture::Work kind_;
    bool didWork_;
    bool active_;
    uint64_t start_;
  };

}  // namespace cpu_utilization
