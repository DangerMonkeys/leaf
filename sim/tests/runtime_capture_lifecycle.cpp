#include <assert.h>
#include <esp_heap_caps.h>
#include <stdio.h>

#include <string>
#include <thread>

#include "diagnostics/cpu_utilization.h"
#include "runtime.h"
#include "sim/clock.h"
#include "storage/sd_card.h"
#include "ui/settings/settings.h"

namespace {
  size_t freeInternal = 200 * 1024;
  void* captureAllocation = nullptr;
  unsigned allocations = 0;
  unsigned releases = 0;
  class StringPrint : public Print {
   public:
    std::string text;
    size_t write(uint8_t c) override {
      text += static_cast<char>(c);
      return 1;
    }
  };
}  // namespace

extern "C" {
void* __real_heap_caps_malloc(size_t, uint32_t);
void __real_heap_caps_free(void*);
size_t __wrap_heap_caps_get_free_size(uint32_t caps) { return freeInternal; }
void* __wrap_heap_caps_malloc(size_t size, uint32_t caps) {
  assert((caps & MALLOC_CAP_INTERNAL) != 0);
  ++allocations;
  captureAllocation = __real_heap_caps_malloc(size, caps);
  return captureAllocation;
}
void __wrap_heap_caps_free(void* memory) {
  assert(memory == captureAllocation);
  ++releases;
  __real_heap_caps_free(memory);
}
}

int main() {
  sim::Runtime::Options options;
  options.cardRoot = "sim/build/runtime-lifecycle-card";
  options.statePath = "sim/build/runtime-lifecycle-state";
  options.speed = 0;
  sim::runtime().configure(options);
  sdcard.init(false);
  settings.dev_mode = false;
  settings.diag_cpuUtilization =
      true;  // A persisted diagnostic opt-in cannot bypass Developer mode.
  assert(!cpu_utilization::start());
  for (unsigned i = 0; i < 100; ++i) {
    cpu_utilization::update();
    cpu_utilization::Scope disabled(runtime_capture::Work::Tasks);
    cpu_utilization::GpsSentenceScope sentence("$GPGGA,", 7);
    cpu_utilization::GpsStageScope parse(runtime_capture::GpsStage::Parse);
  }
  assert(!cpu_utilization::capturing() && allocations == 0);
  settings.diag_cpuUtilization = false;
  settings.dev_mode = true;

  // Insufficient internal headroom must fail without allocating.
  freeInternal = 32 * 1024;
  assert(!cpu_utilization::start());
  assert(allocations == 0 && !cpu_utilization::hasCapture());
  freeInternal = 200 * 1024;
  assert(cpu_utilization::start());
  assert(allocations == 1 && cpu_utilization::capturing());
  {
    cpu_utilization::GpsUartScope fragment(true, true);
    fragment.finish(true);
    cpu_utilization::GpsSentenceScope sentence("$GPGGA,", 7);
    cpu_utilization::gpsValidity(true);
    cpu_utilization::GpsSentenceScope nested("$GPRMC,", 7, true);
    std::thread receiver([] {
      cpu_utilization::Scope callback(runtime_capture::Work::Gps);
      cpu_utilization::GpsSentenceScope sentence("$GPGGA,", 7, true);
      cpu_utilization::GpsStageScope parse(runtime_capture::GpsStage::Parse);
      cpu_utilization::gpsValidity(false);
    });
    receiver.join();
  }
  assert(!cpu_utilization::exportToSd());
  StringPrint active;
  assert(!cpu_utilization::writeJson(active));
  for (unsigned tick = 1; tick <= 110; ++tick) {
    sim::clock().advanceUs(10000);
    cpu_utilization::beginSlot(tick % 100, tick, tick * 10000, sim::clock().nowUs());
    cpu_utilization::Scope work(runtime_capture::Work::Tasks);
  }
  cpu_utilization::freeze();
  StringPrint frozen;
  assert(cpu_utilization::writeJson(frozen));
  assert(frozen.text.find("\"capture_memory\":\"internal\"") != std::string::npos);
  assert(frozen.text.find("\"memory_supported\":false") != std::string::npos);
  assert(frozen.text.find("\"unmeasured_async_segments\":1") != std::string::npos);
  assert(frozen.text.find("\"schema_version\":3") != std::string::npos);
  assert(frozen.text.find("\"excluded_async\":1") != std::string::npos);
  assert(frozen.text.find("\"nested_dropped\":1") != std::string::npos);
  assert(frozen.text.find("\"partial_uart\":1") != std::string::npos);
  assert(frozen.text.find("\"pages\":[") != std::string::npos);
  assert(allocations == 1);  // No allocation per slot or at freeze.
  assert(cpu_utilization::exportToSd());
  assert(releases == 1 && !cpu_utilization::hasCapture());
  assert(cpu_utilization::exportedPath()[0]);
  assert(cpu_utilization::start());
  assert(allocations == 2);
  settings.dev_mode = false;
  cpu_utilization::update();
  assert(!cpu_utilization::capturing());
  assert(cpu_utilization::exportToSd());
  assert(releases == 2);
  puts("Runtime capture FN8 allocation, freeze/export and release checks passed");
}
