#pragma once

#include "diagnostics/runtime_capture.h"

namespace runtime_capture {
  constexpr unsigned GPS_TYPE_COUNT = 8;
  constexpr unsigned GPS_STAGE_COUNT = 7;
  constexpr unsigned GPS_RECENT_COUNT = 8;
  constexpr unsigned GPS_WORST_COUNT = 4;
  constexpr unsigned GPS_HISTOGRAM_COUNT = 8;
  constexpr uint32_t GPS_HISTOGRAM_UPPER_US[GPS_HISTOGRAM_COUNT] = {250,  500,  1000,  2000,
                                                                    4000, 8000, 16000, UINT32_MAX};
  enum class GpsStage : uint8_t {
    UartRead,
    BusWait,
    MutexWait,
    Parse,
    Update,
    ReadingCallbacks,
    BleEnqueue,
    Other
  };
  enum GpsFlags : uint8_t {
    Injected = 1,
    PartialUart = 2,
    ButtonSeen = 4,
    RecoverySeen = 8,
    NoSlot = 16
  };
  struct GpsSample {
    uint64_t firstReadUs = 0;
    uint64_t readyUs = 0;
    uint64_t endUs = 0;
    uint64_t expectedUs = 0;
    uint32_t stages[GPS_STAGE_COUNT] = {};
    uint32_t processingUs = 0;
    uint32_t tick = 0;
    uint16_t length = 0;
    char talker[2] = {};
    uint8_t type = GPS_TYPE_COUNT - 1;
    uint8_t validity = 0;  // 0: parser unobserved; 1: rejected; 2: accepted checksum.
    uint8_t slot = 255;
    uint8_t flags = 0;
  };
  inline uint8_t gpsType(const char* text, unsigned length) {
    static constexpr const char* types[GPS_TYPE_COUNT - 1] = {"GGA", "RMC", "GSV", "GSA",
                                                              "VTG", "GLL", "ZDA"};
    if (length >= 7 && text[0] == '$' && text[6] == ',')
      for (unsigned i = 0; i < GPS_TYPE_COUNT - 1; ++i)
        if (text[3] == types[i][0] && text[4] == types[i][1] && text[5] == types[i][2]) return i;
    return GPS_TYPE_COUNT - 1;
  }
  inline uint32_t gpsEndDelay(const GpsSample& s) {
    return s.flags & NoSlot ? 0 : late(s.endUs, s.expectedUs + SLOT_US);
  }
  struct GpsStats {
    uint64_t totalUs = 0;
    uint64_t stages[GPS_STAGE_COUNT] = {};
    uint32_t count = 0;
    uint32_t accepted = 0;
    uint32_t rejected = 0;
    uint32_t crossings = 0;
    uint32_t maxUs = 0;
    uint32_t maxEndDelayUs = 0;
    uint32_t maxStages[GPS_STAGE_COUNT] = {};
    uint32_t histogram[GPS_HISTOGRAM_COUNT] = {};
  };
  struct GpsProfile {
    GpsStats types[GPS_TYPE_COUNT];
    GpsSample recent[GPS_RECENT_COUNT];
    GpsSample worst[GPS_WORST_COUNT];
    uint32_t startSlots[SLOT_COUNT] = {};
    uint32_t total = 0;
    uint32_t nestedDropped = 0;
    uint32_t uartResets = 0;
    uint32_t unassigned = 0;
    uint32_t partialUart = 0;
    uint32_t recordMaxUs = 0;
    GpsSample current;
    uint64_t dispatchWorkStart = 0;
    uint64_t stageWorkStart = 0;
    uint8_t stage = static_cast<uint8_t>(GpsStage::Other);
    bool active = false;

    void record(const GpsSample& sample) {
      GpsStats& s = types[sample.type];
      s.count++;
      s.accepted += sample.validity == 2;
      s.rejected += sample.validity == 1;
      const uint32_t delay = gpsEndDelay(sample);
      if (delay) s.crossings++;
      if (delay > s.maxEndDelayUs) s.maxEndDelayUs = delay;
      s.totalUs += sample.processingUs;
      if (sample.processingUs > s.maxUs) s.maxUs = sample.processingUs;
      for (unsigned i = 0; i < GPS_STAGE_COUNT; ++i) {
        s.stages[i] += sample.stages[i];
        if (sample.stages[i] > s.maxStages[i]) s.maxStages[i] = sample.stages[i];
      }
      for (unsigned i = 0; i < GPS_HISTOGRAM_COUNT; ++i)
        if (sample.processingUs <= GPS_HISTOGRAM_UPPER_US[i]) {
          s.histogram[i]++;
          break;
        }
      recent[total++ % GPS_RECENT_COUNT] = sample;
      if (sample.flags & NoSlot)
        unassigned++;
      else
        startSlots[sample.slot]++;
      if (sample.flags & PartialUart) partialUart++;
      if (delay) {
        unsigned replace = 0;
        for (unsigned i = 1; i < GPS_WORST_COUNT; ++i)
          if (gpsEndDelay(worst[i]) < gpsEndDelay(worst[replace])) replace = i;
        if (!worst[replace].endUs || delay > gpsEndDelay(worst[replace]) ||
            (delay == gpsEndDelay(worst[replace]) &&
             sample.processingUs > worst[replace].processingUs))
          worst[replace] = sample;
      }
    }
    uint8_t enter(uint8_t next, uint64_t workUs) {
      const uint8_t previous = stage;
      if (stage < GPS_STAGE_COUNT) current.stages[stage] += workUs - stageWorkStart;
      stage = next;
      stageWorkStart = workUs;
      return previous;
    }
  };
}  // namespace runtime_capture
