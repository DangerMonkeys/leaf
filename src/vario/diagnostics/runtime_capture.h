#pragma once

#include <stdint.h>

// Platform-independent accounting. Storage and work per segment/slot are bounded.
namespace runtime_capture {
  constexpr uint32_t SLOT_US = 10000;
  constexpr unsigned SLOT_COUNT = 100;
  constexpr unsigned TRACE_COUNT = 8;
  constexpr unsigned HISTOGRAM_COUNT = 16;
  constexpr uint32_t HISTOGRAM_UPPER_US[HISTOGRAM_COUNT] = {
      250,  500,  1000, 2000, 3000, 4000,  5000,  6000,
      7000, 7500, 8000, 8500, 9000, 10000, 20000, UINT32_MAX};
  enum class Work : uint8_t { Tasks, Gps, Services, Poll, Count };
  constexpr unsigned PAGE_COUNT = 7;  // Render contexts 10..16; menus/overlays are excluded.

  struct Slot {
    uint64_t expectedUs = 0;
    uint64_t startUs = 0;
    uint64_t tasksEndUs = 0;
    uint64_t lastWorkEndUs = 0;
    uint64_t closedUs = 0;
    uint32_t tick = 0;
    uint32_t skippedTicks = 0;
    uint32_t workUs[static_cast<unsigned>(Work::Count)] = {};
    uint32_t bookkeepingUs = 0;
    uint32_t baroStartDelayUs = 0;
    uint8_t index = 0;
    uint8_t buttons = 0;
    uint8_t buttonEvents = 0;
    uint8_t display = 0;
    uint8_t pageMask = 0;
    bool recovery = false;
    bool complete = false;
    bool baroRecorded = false;
  };
  inline uint32_t late(uint64_t actual, uint64_t expected) {
    return actual > expected ? static_cast<uint32_t>(actual - expected) : 0;
  }
  inline uint32_t occupied(const Slot& slot) {
    // Empty UART polling is disclosed separately from useful foreground work.
    return slot.workUs[0] + slot.workUs[1] + slot.workUs[2];
  }
  struct Stats {
    uint32_t count = 0;
    uint32_t deadlineMisses = 0;
    uint32_t durationOverruns = 0;
    uint32_t skippedTicks = 0;
    uint32_t maxOccupiedUs = 0;
    uint32_t maxStartDelayUs = 0;
    uint32_t maxEndDelayUs = 0;
    uint32_t maxBaroStartDelayUs = 0;
    uint64_t workUs[static_cast<unsigned>(Work::Count)] = {};
    uint32_t histogram[HISTOGRAM_COUNT] = {};
  };
  struct Excluded {
    uint32_t buttons = 0;
    uint32_t recovery = 0;
    uint32_t recoveryMisses = 0;
    uint32_t recoverySkipped = 0;
    uint32_t maxRecoveryStartUs = 0;
    uint32_t maxRecoveryEndUs = 0;
    uint32_t maxRecoveryBaroUs = 0;
    uint32_t maxRecoveryOccupiedUs = 0;
  };
  struct Frames {
    uint64_t totalUs = 0;
    uint32_t count = 0;
    uint32_t maxUs = 0;
    uint32_t histogram[HISTOGRAM_COUNT] = {};
    void add(uint32_t elapsed) {
      totalUs += elapsed;
      count++;
      if (elapsed > maxUs) maxUs = elapsed;
      for (unsigned i = 0; i < HISTOGRAM_COUNT; ++i)
        if (elapsed <= HISTOGRAM_UPPER_US[i]) {
          histogram[i]++;
          break;
        }
    }
  };
  struct Page {
    Frames frames;
    uint32_t slots = 0;
    uint32_t maxSlotUs = 0;
    uint32_t deadlineMisses = 0;
    uint32_t buttonFrames = 0;
    uint32_t recoveryFrames = 0;
  };
  inline bool buttonAffected(const Slot& slot) { return slot.buttons || slot.buttonEvents; }
  inline void accumulate(Stats& s, const Slot& slot) {
    s.count++;
    const uint32_t used = occupied(slot);
    const uint32_t startDelay = late(slot.startUs, slot.expectedUs);
    const uint32_t endDelay = late(slot.lastWorkEndUs, slot.expectedUs + SLOT_US);
    if (endDelay) s.deadlineMisses++;
    if (used > SLOT_US) s.durationOverruns++;
    s.skippedTicks += slot.skippedTicks;
    if (used > s.maxOccupiedUs) s.maxOccupiedUs = used;
    if (startDelay > s.maxStartDelayUs) s.maxStartDelayUs = startDelay;
    if (endDelay > s.maxEndDelayUs) s.maxEndDelayUs = endDelay;
    if (slot.baroStartDelayUs > s.maxBaroStartDelayUs)
      s.maxBaroStartDelayUs = slot.baroStartDelayUs;
    for (unsigned i = 0; i < static_cast<unsigned>(Work::Count); ++i) s.workUs[i] += slot.workUs[i];
    for (unsigned i = 0; i < HISTOGRAM_COUNT; ++i)
      if (used <= HISTOGRAM_UPPER_US[i]) {
        s.histogram[i]++;
        break;
      }
  }
  inline bool worse(const Slot& candidate, const Slot& previous) {
    const uint32_t delay = late(candidate.lastWorkEndUs, candidate.expectedUs + SLOT_US);
    const uint32_t oldDelay = late(previous.lastWorkEndUs, previous.expectedUs + SLOT_US);
    return !previous.complete || delay > oldDelay ||
           (delay == oldDelay && occupied(candidate) > occupied(previous));
  }
  class Recorder {
   public:
    Stats stats[SLOT_COUNT] = {};
    Excluded excluded[SLOT_COUNT] = {};
    Stats buttonStats;
    Stats recoveryStats;
    Page pages[PAGE_COUNT];
    Frames currentPages[PAGE_COUNT];
    Slot trace[TRACE_COUNT] = {};
    Slot worst = {};
    Slot worstPrevious = {};
    Slot current = {};
    Slot worstRecovery = {};
    Slot recoveryPrevious = {};
    uint32_t traceTotal = 0;
    uint32_t partialSlots = 0;
    uint32_t bookkeepingMaxUs = 0;
    uint32_t unassignedWorkUs = 0;
    bool open = false;
    uint8_t recoveryRemaining = 0;
    void begin(uint8_t index, uint32_t tick, uint64_t expected, uint64_t actual) {
      const bool hadPrevious = open;
      const uint32_t previousTick = current.tick;
      close(actual, true);
      current = {};
      current.index = index;
      current.tick = tick;
      current.expectedUs = expected;
      current.startUs = actual;
      currentPages[0] = {};  // Clear all pending renders before the new slot.
      for (unsigned i = 1; i < PAGE_COUNT; ++i) currentPages[i] = {};
      if (recoveryRemaining) {
        current.recovery = true;
        recoveryRemaining--;
      }
      current.tasksEndUs = actual;
      current.lastWorkEndUs = actual;
      if (hadPrevious) current.skippedTicks = tick - previousTick - 1;
      open = true;
    }
    void add(Work kind, uint32_t measuredUs, uint64_t actualEnd, bool didWork = true) {
      if (!open) {
        unassignedWorkUs += measuredUs;
        return;
      }
      current.workUs[static_cast<unsigned>(kind)] += measuredUs;
      if (kind != Work::Poll && didWork) current.lastWorkEndUs = actualEnd;
    }
    void rendered(uint8_t context, uint32_t elapsed) {
      if (!open || context < 10 || context >= 10 + PAGE_COUNT) return;
      current.display = context;
      current.pageMask |= 1u << (context - 10);
      currentPages[context - 10].add(elapsed);
    }
    void close(uint64_t now, bool complete) {
      if (!open) return;
      current.closedUs = now;
      current.complete = complete;
      trace[traceTotal % TRACE_COUNT] = current;
      traceTotal++;
      if (complete && current.index < SLOT_COUNT) {
        const uint32_t used = occupied(current);
        const uint32_t endDelay = late(current.lastWorkEndUs, current.expectedUs + SLOT_US);
        const bool button = buttonAffected(current);
        Excluded& e = excluded[current.index];
        if (button) {
          e.buttons++;
          accumulate(buttonStats, current);
          recoveryRemaining = 2;
        } else if (current.recovery) {
          e.recovery++;
          accumulate(recoveryStats, current);
          if (endDelay) e.recoveryMisses++;
          e.recoverySkipped += current.skippedTicks;
          const uint32_t startDelay = late(current.startUs, current.expectedUs);
          if (startDelay > e.maxRecoveryStartUs) e.maxRecoveryStartUs = startDelay;
          if (endDelay > e.maxRecoveryEndUs) e.maxRecoveryEndUs = endDelay;
          if (current.baroStartDelayUs > e.maxRecoveryBaroUs)
            e.maxRecoveryBaroUs = current.baroStartDelayUs;
          if (used > e.maxRecoveryOccupiedUs) e.maxRecoveryOccupiedUs = used;
          // Retain a separate recovery window until two following dispatches settle.
          if (endDelay || startDelay > 1000) recoveryRemaining = 2;
          if (worse(current, worstRecovery)) {
            worstRecovery = current;
            if (traceTotal > 1) recoveryPrevious = trace[(traceTotal - 2) % TRACE_COUNT];
          }
        } else {
          accumulate(stats[current.index], current);
          if (worse(current, worst)) {
            worst = current;
            if (traceTotal > 1) worstPrevious = trace[(traceTotal - 2) % TRACE_COUNT];
          }
        }
        for (unsigned i = 0; i < PAGE_COUNT; ++i) {
          const Frames& f = currentPages[i];
          Page& p = pages[i];
          if (!f.count) continue;
          if (button)
            p.buttonFrames += f.count;
          else if (current.recovery)
            p.recoveryFrames += f.count;
          else {
            p.frames.count += f.count;
            p.frames.totalUs += f.totalUs;
            if (f.maxUs > p.frames.maxUs) p.frames.maxUs = f.maxUs;
            for (unsigned j = 0; j < HISTOGRAM_COUNT; ++j) p.frames.histogram[j] += f.histogram[j];
            p.slots++;
            if (used > p.maxSlotUs) p.maxSlotUs = used;
            if (endDelay) p.deadlineMisses++;
          }
        }
      } else {
        partialSlots++;
      }
      open = false;
    }
  };
}  // namespace runtime_capture
