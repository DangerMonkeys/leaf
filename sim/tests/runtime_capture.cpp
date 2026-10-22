#include <assert.h>
#include <stdio.h>

#include "diagnostics/gps_profile.h"
#include "diagnostics/runtime_capture.h"

using namespace runtime_capture;

int main() {
  Recorder r;
  // Assigned work alone fits; GPS then pushes the slot past its deadline.
  r.begin(0, 1, 10000, 10000);
  r.add(Work::Tasks, 8000, 18000);
  r.current.tasksEndUs = 18000;
  r.add(Work::Gps, 4000, 22000);
  r.begin(1, 2, 20000, 23000);
  r.add(Work::Tasks, 8000, 31000);
  r.current.tasksEndUs = 31000;
  r.begin(2, 3, 30000, 31000);
  assert(r.stats[0].deadlineMisses == 1 && r.stats[0].durationOverruns == 1);
  // This slot fits in 10ms of work but its inherited delay causes a deadline miss.
  assert(r.stats[1].maxOccupiedUs == 8000);
  assert(r.stats[1].maxStartDelayUs == 3000);
  assert(r.stats[1].deadlineMisses == 1 && r.stats[1].durationOverruns == 0);
  assert(r.trace[1].startUs == 23000 && r.trace[1].tasksEndUs == 31000);

  // Empty polls are disclosed, not counted as useful work or a late completion.
  r.add(Work::Tasks, 1000, 32000);
  r.add(Work::Poll, 7000, 41000, false);
  r.begin(3, 6, 60000, 60000);
  assert(r.stats[2].maxOccupiedUs == 1000 && r.stats[2].deadlineMisses == 0);
  assert(r.stats[2].workUs[3] == 7000);
  r.begin(4, 7, 70000, 70000);
  assert(r.stats[3].skippedTicks == 2);
  r.close(71000, false);
  assert(r.partialSlots == 1 && r.stats[4].count == 0);

  // Timing survives micros() rollover because the capture uses 64-bit timestamps.
  Recorder wrap;
  const uint64_t t = uint64_t(UINT32_MAX) + 100000;
  wrap.begin(0, UINT32_MAX, t, t);
  wrap.add(Work::Tasks, 1500, t + 1500);
  wrap.begin(1, 0, t + 10000, t + 10000);
  assert(wrap.current.skippedTicks == 0 && wrap.stats[0].count == 1);

  // Ring overwrite does not discard aggregate statistics or the worst incident.
  for (unsigned i = 0; i < 1000; ++i) {
    const uint64_t start = 100000 + uint64_t(i) * 10000;
    r.begin(i % 100, 10 + i, start, start);
    r.add(Work::Tasks, 100, start + 100);
  }
  assert(r.traceTotal > TRACE_COUNT);
  assert(r.worst.workUs[0] == 8000 && r.worst.workUs[1] == 4000);
  assert(r.stats[0].count == 11);
  assert(r.stats[0].histogram[0] == 10);
  assert(r.stats[0].histogram[HISTOGRAM_COUNT - 2] == 1);
  // A button/inline refresh and the downstream catch-up slots stay outside the baseline.
  Recorder interaction;
  interaction.begin(0, 1, 10000, 10000);
  interaction.current.buttonEvents = 1;
  interaction.rendered(10, 12000);
  interaction.add(Work::Tasks, 30000, 40000);
  interaction.begin(1, 4, 40000, 40000);
  interaction.add(Work::Tasks, 1000, 41000);
  assert(interaction.stats[0].count == 0 && interaction.excluded[0].buttons == 1);
  assert(interaction.pages[0].buttonFrames == 1 && interaction.pages[0].frames.count == 0);
  interaction.begin(2, 5, 50000, 70000);  // Recovery starts 20ms late but still executes.
  interaction.rendered(11, 500);
  interaction.add(Work::Tasks, 1000, 71000);
  interaction.begin(3, 8, 80000, 80000);
  assert(interaction.excluded[1].recoverySkipped == 2);
  assert(interaction.excluded[2].maxRecoveryStartUs == 20000);
  assert(interaction.excluded[2].recoveryMisses == 1);
  assert(interaction.pages[1].recoveryFrames == 1);
  interaction.add(Work::Tasks, 1000, 81000);
  interaction.begin(4, 9, 90000, 90000);
  interaction.add(Work::Tasks, 1000, 91000);
  interaction.begin(5, 10, 100000, 100000);
  assert(!interaction.current.recovery);
  interaction.rendered(11, 1500);
  interaction.rendered(11, 2000);
  interaction.rendered(20, 30000);  // Menus must not pollute page statistics.
  interaction.add(Work::Tasks, 4000, 104000);
  interaction.begin(6, 11, 110000, 110000);
  assert(interaction.stats[5].count == 1 && interaction.stats[5].maxOccupiedUs == 4000);
  assert(interaction.pages[1].frames.count == 2 && interaction.pages[1].frames.maxUs == 2000);
  assert(interaction.pages[1].frames.totalUs == 3500 && interaction.pages[1].slots == 1);
  assert(interaction.worstRecovery.index == 2 && interaction.recoveryPrevious.index == 1);
  assert(interaction.worstRecovery.pageMask == 2);
  // Held pins exclude a slot even without a dispatched click event.
  interaction.current.buttons = 1;
  interaction.add(Work::Tasks, 500, 110500);
  interaction.begin(7, 12, 120000, 120000);
  assert(interaction.excluded[6].buttons == 1 && interaction.stats[6].count == 0);
  // Exclusive stage accounting: a nested mutex wait must not also count as callback time.
  GpsProfile profile;
  profile.stageWorkStart = 100;
  assert(profile.enter(static_cast<uint8_t>(GpsStage::Parse), 110) ==
         static_cast<uint8_t>(GpsStage::Other));
  profile.enter(static_cast<uint8_t>(GpsStage::MutexWait), 210);
  profile.enter(static_cast<uint8_t>(GpsStage::Parse), 260);
  profile.enter(static_cast<uint8_t>(GpsStage::Other), 310);
  assert(profile.current.stages[static_cast<unsigned>(GpsStage::Parse)] == 150);
  assert(profile.current.stages[static_cast<unsigned>(GpsStage::MutexWait)] == 50);
  assert(gpsType("$GNGGA,", 7) == 0 && gpsType("$GPRMC,", 7) == 1);
  assert(gpsType("$GPGSV,", 7) == 2 && gpsType("bad", 3) == GPS_TYPE_COUNT - 1);
  for (unsigned i = 0; i < 50; ++i) {
    GpsSample s;
    s.firstReadUs = 5000;
    s.readyUs = 19000;
    s.expectedUs = 10000;
    s.endUs = 20000 + i;
    s.processingUs = 1000 + i;
    s.slot = 76;
    s.type = 0;
    s.validity = i % 2 ? 2 : 1;
    profile.record(s);
  }
  assert(profile.types[0].count == 50 && profile.types[0].accepted == 25 &&
         profile.types[0].rejected == 25);
  assert(profile.types[0].crossings == 49 && profile.startSlots[76] == 50);
  assert(profile.total > GPS_RECENT_COUNT && profile.types[0].maxEndDelayUs == 49);
  for (const auto& s : profile.worst) assert(gpsEndDelay(s) >= 46);
  GpsSample noSlot;
  noSlot.flags = NoSlot | PartialUart;
  noSlot.type = 1;
  noSlot.endUs = 30000;
  profile.record(noSlot);
  assert(profile.unassigned == 1 && profile.partialUart == 1 && profile.types[1].crossings == 0);
  puts("runtime capture accounting: PASS");
}
