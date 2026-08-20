// The emulated device's wall clock.
//
// The firmware sets the system time from the GPS (instruments/gps.cpp calls settimeofday), and a
// great deal hangs off that succeeding: Flight::startFlight refuses to record until the clock has
// been synced this boot, and IGC files, bus logs and logbook entries are all named from it.
//
// On the host that call cannot work -- setting the machine's clock needs privileges the emulator
// has no business holding, and inside a container it fails outright -- so these definitions
// interpose on libc and give the emulated device a clock of its own.  It starts from the host's
// time, and once the firmware syncs it from a recording's GPS fixes it runs on virtual time, which
// is what keeps timestamps consistent when a flight is replayed faster than real time.

#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>

#include "sim/clock.h"

namespace {

  // Offset from virtual device time to the emulated wall clock, in microseconds.
  std::atomic<int64_t> g_offsetUs{0};
  std::atomic<bool> g_synced{false};

  int64_t hostRealtimeUs() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
  }

  int64_t emulatedNowUs() {
    return g_synced.load() ? (int64_t)sim::clock().nowUs() + g_offsetUs.load() : hostRealtimeUs();
  }

}  // namespace

extern "C" {

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
  (void)tz;
  if (!tv) return -1;
  const int64_t wanted = (int64_t)tv->tv_sec * 1000000 + tv->tv_usec;
  g_offsetUs.store(wanted - (int64_t)sim::clock().nowUs());
  g_synced.store(true);
  return 0;
}

int gettimeofday(struct timeval* tv, void* tz) {
  (void)tz;
  if (!tv) return -1;
  const int64_t now = emulatedNowUs();
  tv->tv_sec = (time_t)(now / 1000000);
  tv->tv_usec = (suseconds_t)(now % 1000000);
  return 0;
}

time_t time(time_t* out) {
  const time_t seconds = (time_t)(emulatedNowUs() / 1000000);
  if (out) *out = seconds;
  return seconds;
}

}  // extern "C"

namespace sim {
  bool systemClockSynced() { return g_synced.load(); }
}  // namespace sim
