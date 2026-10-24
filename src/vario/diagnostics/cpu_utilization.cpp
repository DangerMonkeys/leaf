#include "diagnostics/cpu_utilization.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#ifdef LEAF_SIM
#include <time.h>
#include <thread>
#else
#include <esp_timer.h>
#endif

#include "diagnostics/diagnostic_logs.h"
#include "diagnostics/heap_monitor.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"
#include "variant.h"

namespace cpu_utilization {
  std::atomic<bool> captureActive{false};
  namespace {
    using namespace runtime_capture;
    constexpr unsigned MEMORY_COUNT = 16;
    constexpr size_t INTERNAL_RESERVE_BYTES = 32 * 1024;
    struct Memory {
      uint64_t timeUs = 0;
      heap_monitor::Snapshot value = {};
    };
    struct Capture {
      Recorder recorder;
      GpsProfile gps;
      struct {
        uint64_t firstReadUs = 0;
        uint64_t chunkUs = 0;
        uint64_t chunkWorkUs = 0;
        uint32_t readUs = 0;
        bool partial = false;
        bool chunkPartial = false;
      } uart;
      Memory memory[MEMORY_COUNT];
      Memory firstMemory;
      Memory memoryMinima;
      char settingsJson[400] = {};
      bool settingsChanged = false;
      uint32_t memoryTotal = 0;
      uint32_t memoryMaxUs = 0;
      uint32_t clockPairMaxUs = 0;
      uint64_t startUs = 0;
      uint64_t endUs = 0;
    };
    static_assert(sizeof(Capture) <= 22832,
                  "Keep GPS profiling within 512 bytes of the prior capture footprint");
    Capture* data = nullptr;
    std::atomic<uint32_t> failedAllocations{0};
    std::atomic<uint32_t> excludedGps{0};
    bool failedAllocationHook = false;
    uint32_t timerUncertaintyUs = 0;
    bool previouslyEnabled = false;
    bool allocationFailed = false;
    uint64_t lastMemoryUs = 0;
    uint32_t exportSequence = 0;
    char lastExportPath[80] = {};
#ifdef LEAF_SIM
    std::atomic<uint32_t> foreignSegments{0};
#else
    std::atomic<TaskHandle_t> captureOwner{nullptr};
#endif
    bool onCaptureThread() {
      // Initialized by start(), before capture is published to other tasks.
#ifdef LEAF_SIM
      static const auto owner = std::this_thread::get_id();
      return std::this_thread::get_id() == owner;
#else
      return xTaskGetCurrentTaskHandle() == captureOwner.load(std::memory_order_relaxed);
#endif
    }

    bool gpsProfiling() { return capturing() && onCaptureThread() && data->gps.active; }

    void settingsJson(char* out, size_t length) {
      snprintf(
          out, length,
          "{\"ble\":%s,\"fanet_region\":%u,\"thermal_core\":%s,\"thermal_track\":%s,"
          "\"basic_page\":%s,\"user_page\":%s,\"nav_page\":%s,\"debug_pages\":%s,"
          "\"system_log\":%s,\"network_log\":%s,\"web_log\":%s,\"vario_log\":%s,\"fanet_log\":%s}",
          settings.system_bluetoothOn ? "true" : "false", (unsigned)settings.fanet_region,
          settings.disp_showThermalCorePage ? "true" : "false",
          settings.disp_showThermalTrackPage ? "true" : "false",
          settings.disp_showBasicPage ? "true" : "false",
          settings.disp_showUserPage ? "true" : "false",
          settings.disp_showNavPage ? "true" : "false",
          settings.disp_showDebugPage ? "true" : "false",
          settings.diag_systemEvents ? "true" : "false",
          settings.diag_networkEvents ? "true" : "false",
          settings.diag_webRequests ? "true" : "false", settings.diag_vario ? "true" : "false",
          settings.dev_logFanet ? "true" : "false");
    }

#ifndef LEAF_SIM
    void allocationFailure(size_t size, uint32_t caps, const char* function) {
      if (captureActive.load(std::memory_order_relaxed))
        failedAllocations.fetch_add(1, std::memory_order_relaxed);
    }
#endif

    class BufferedFilePrint : public Print {
     public:
      explicit BufferedFilePrint(File& file) : file_(file) {}
      size_t write(uint8_t byte) override {
        buffer_[used_++] = byte;
        if (used_ == sizeof(buffer_)) flushBuffer();
        return failed_ ? 0 : 1;
      }
      void flushBuffer() {
        if (used_ && file_.write(buffer_, used_) != used_) failed_ = true;
        used_ = 0;
      }
      bool failed() const { return failed_; }

     private:
      File& file_;
      uint8_t buffer_[512];
      size_t used_ = 0;
      bool failed_ = false;
    };

    const char* slotLabel(unsigned index) {
      switch (index) {
        case 9:
        case 59:
          return "gps";
        case 19:
          return "thermal_detect_nav";
        case 29:
          return "igc_log";
        case 39:
        case 89:
          return "display";
        case 49:
          return "ambient_start";
        case 69:
          return "thermal_nav";
        case 79:
          return "power_sd";
        case 99:
          return "ambient_read";
      }
      switch (index % 10) {
        case 0:
        case 5:
          return "baro_adc";
        case 2:
          return "imu_wind";
        case 7:
          return "imu";
        default:
          return "base";
      }
    }

    void captureMemory() {
      if (!data || !capturing()) return;
#ifndef LEAF_SIM
      const uint64_t begin = nowUs();
      Memory& sample = data->memory[data->memoryTotal % MEMORY_COUNT];
      sample.timeUs = begin;
      heap_monitor::snapshot(sample.value);
      if (data->memoryTotal == 0) {
        data->firstMemory = sample;
        data->memoryMinima = sample;
      } else {
        auto& m = data->memoryMinima.value;
        const auto& s = sample.value;
#define KEEP_MIN(field) \
  if (s.field < m.field) m.field = s.field
        KEEP_MIN(internalFree);
        KEEP_MIN(internalMin);
        KEEP_MIN(internalLargest);
#define KEEP_STACK_MIN(field) \
  if (s.field && (!m.field || s.field < m.field)) m.field = s.field
        KEEP_STACK_MIN(loopStack);
        KEEP_STACK_MIN(bleStack);
        KEEP_STACK_MIN(fanetTxStack);
        KEEP_STACK_MIN(fanetRxStack);
#undef KEEP_STACK_MIN
#undef KEEP_MIN
      }
      data->memoryTotal++;
      const uint32_t elapsed = nowUs() - begin;
      if (elapsed > data->memoryMaxUs) data->memoryMaxUs = elapsed;
      // Observer work consumes real deadline headroom and stays in the totals.
      data->recorder.add(Work::Services, elapsed, nowUs());
#endif
      lastMemoryUs = nowUs();
    }

    void stringJson(Print& out, const char* value) {
      out.print('"');
      for (const unsigned char* c = reinterpret_cast<const unsigned char*>(value); *c; ++c) {
        if (*c == '"' || *c == '\\') out.print('\\');
        if (*c < 32)
          out.printf("\\u%04x", *c);
        else
          out.write(*c);
      }
      out.print('"');
    }

    void slotJson(Print& out, const Slot& s) {
      out.printf(
          "{\"slot\":%u,\"tick\":%lu,\"expected_us\":%llu,\"start_us\":%llu,"
          "\"tasks_end_us\":%llu,\"last_work_end_us\":%llu,\"closed_us\":%llu,"
          "\"skipped_ticks\":%lu,\"tasks_us\":%lu,\"gps_us\":%lu,"
          "\"services_us\":%lu,\"poll_us\":%lu,\"bookkeeping_us\":%lu,"
          "\"baro_start_delay_us\":%lu,\"buttons\":%u,\"button_events\":%u,"
          "\"display_context\":%u,\"display_page_mask\":%u,\"button_recovery\":%s,\"complete\":%s}",
          s.index, (unsigned long)s.tick, (unsigned long long)s.expectedUs,
          (unsigned long long)s.startUs, (unsigned long long)s.tasksEndUs,
          (unsigned long long)s.lastWorkEndUs, (unsigned long long)s.closedUs,
          (unsigned long)s.skippedTicks, (unsigned long)s.workUs[0], (unsigned long)s.workUs[1],
          (unsigned long)s.workUs[2], (unsigned long)s.workUs[3], (unsigned long)s.bookkeepingUs,
          (unsigned long)s.baroStartDelayUs, s.buttons, s.buttonEvents, s.display, s.pageMask,
          s.recovery ? "true" : "false", s.complete ? "true" : "false");
    }

    void memoryJson(Print& out, const Memory& m) {
      const auto& s = m.value;
      out.printf(
          "{\"time_us\":%llu,\"internal_free\":%lu,\"internal_min\":%lu,"
          "\"internal_largest\":%lu,\"loop_stack\":%lu,\"ble_stack\":%lu,"
          "\"fanet_tx_stack\":%lu,\"fanet_rx_stack\":%lu}",
          (unsigned long long)m.timeUs, (unsigned long)s.internalFree, (unsigned long)s.internalMin,
          (unsigned long)s.internalLargest, (unsigned long)s.loopStack, (unsigned long)s.bleStack,
          (unsigned long)s.fanetTxStack, (unsigned long)s.fanetRxStack);
    }

    void totalsJson(Print& out, const Stats& s) {
      out.printf(
          "{\"count\":%lu,\"deadline_misses\":%lu,\"duration_overruns\":%lu,"
          "\"skipped_ticks\":%lu,\"max_occupied_us\":%lu,\"max_start_delay_us\":%lu,"
          "\"max_end_delay_us\":%lu,\"max_baro_start_delay_us\":%lu}",
          (unsigned long)s.count, (unsigned long)s.deadlineMisses,
          (unsigned long)s.durationOverruns, (unsigned long)s.skippedTicks,
          (unsigned long)s.maxOccupiedUs, (unsigned long)s.maxStartDelayUs,
          (unsigned long)s.maxEndDelayUs, (unsigned long)s.maxBaroStartDelayUs);
    }

    void gpsSampleJson(Print& out, const GpsSample& s) {
      out.printf("{\"type\":%u,\"talker\":", s.type);
      const char talker[] = {s.talker[0], s.talker[1], 0};
      stringJson(out, talker);
      out.printf(
          ",\"validity\":%u,\"slot\":%u,\"tick\":%lu,\"flags\":%u,\"length\":%u,"
          "\"first_read_us\":%llu,\"ready_us\":%llu,\"end_us\":%llu,\"expected_us\":%llu,"
          "\"processing_us\":%lu,\"stage_us\":[",
          s.validity, s.slot, (unsigned long)s.tick, s.flags, s.length,
          (unsigned long long)s.firstReadUs, (unsigned long long)s.readyUs,
          (unsigned long long)s.endUs, (unsigned long long)s.expectedUs,
          (unsigned long)s.processingUs);
      for (unsigned i = 0; i < GPS_STAGE_COUNT; ++i)
        out.printf("%s%lu", i ? "," : "", (unsigned long)s.stages[i]);
      out.print("]}");
    }

    void gpsJson(Print& out, const GpsProfile& p) {
      out.printf(
          "{\"total\":%lu,\"nested_dropped\":%lu,\"uart_resets\":%lu,\"unassigned\":%lu,"
          "\"partial_uart\":%lu,\"excluded_async\":%lu,\"record_max_us\":%lu,"
          "\"recent_capacity\":%u,\"worst_capacity\":%u,\"recent_overwritten\":%lu,"
          "\"stage_names\":[\"UART read\",\"Bus mutex wait\",\"Mutex wait\",\"Parse\","
          "\"GPS updates\",\"Reading callbacks\",\"BLE enqueue\"],\"histogram_upper_us\":[",
          (unsigned long)p.total, (unsigned long)p.nestedDropped, (unsigned long)p.uartResets,
          (unsigned long)p.unassigned, (unsigned long)p.partialUart,
          (unsigned long)excludedGps.load(), (unsigned long)p.recordMaxUs, GPS_RECENT_COUNT,
          GPS_WORST_COUNT,
          (unsigned long)(p.total > GPS_RECENT_COUNT ? p.total - GPS_RECENT_COUNT : 0));
      for (unsigned i = 0; i < GPS_HISTOGRAM_COUNT; ++i)
        out.printf("%s%lu", i ? "," : "", (unsigned long)GPS_HISTOGRAM_UPPER_US[i]);
      out.print("],\"start_slots\":[");
      for (unsigned i = 0; i < SLOT_COUNT; ++i)
        out.printf("%s%lu", i ? "," : "", (unsigned long)p.startSlots[i]);
      out.print("],\"types\":[");
      static constexpr const char* names[GPS_TYPE_COUNT] = {"GGA", "RMC", "GSV", "GSA",
                                                            "VTG", "GLL", "ZDA", "Other"};
      for (unsigned i = 0; i < GPS_TYPE_COUNT; ++i) {
        const auto& s = p.types[i];
        out.printf(
            "%s{\"type\":%u,\"label\":\"%s\",\"count\":%lu,\"accepted\":%lu,"
            "\"rejected\":%lu,\"unobserved\":%lu,\"deadline_crossings\":%lu,"
            "\"total_us\":%llu,\"max_processing_us\":%lu,\"max_end_delay_us\":%lu,\"stage_total_"
            "us\":[",
            i ? "," : "", i, names[i], (unsigned long)s.count, (unsigned long)s.accepted,
            (unsigned long)s.rejected, (unsigned long)(s.count - s.accepted - s.rejected),
            (unsigned long)s.crossings, (unsigned long long)s.totalUs, (unsigned long)s.maxUs,
            (unsigned long)s.maxEndDelayUs);
        for (unsigned j = 0; j < GPS_STAGE_COUNT; ++j)
          out.printf("%s%llu", j ? "," : "", (unsigned long long)s.stages[j]);
        out.print("],\"stage_max_us\":[");
        for (unsigned j = 0; j < GPS_STAGE_COUNT; ++j)
          out.printf("%s%lu", j ? "," : "", (unsigned long)s.maxStages[j]);
        out.print("],\"histogram\":[");
        for (unsigned j = 0; j < GPS_HISTOGRAM_COUNT; ++j)
          out.printf("%s%lu", j ? "," : "", (unsigned long)s.histogram[j]);
        out.print("]}");
      }
      out.print("],\"recent\":[");
      const unsigned count = p.total < GPS_RECENT_COUNT ? p.total : GPS_RECENT_COUNT;
      for (unsigned i = 0; i < count; ++i) {
        if (i) out.print(',');
        gpsSampleJson(out, p.recent[(p.total - count + i) % GPS_RECENT_COUNT]);
      }
      out.print("],\"worst\":[");
      bool comma = false;
      for (const auto& s : p.worst)
        if (s.endUs) {
          if (comma) out.print(',');
          gpsSampleJson(out, s);
          comma = true;
        }
      out.print("]}");
    }
  }  // namespace

  uint64_t nowUs() {
#ifdef LEAF_SIM
    return esp_timer_get_time();
#else
    return static_cast<uint64_t>(esp_timer_get_time());
#endif
  }

  uint64_t workClockUs() {
#ifdef LEAF_SIM
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
#else
    return nowUs();
#endif
  }

  bool enabled() { return diagnostic_logs::enabled(diagnostic_logs::Log::CpuUtilization); }
  bool hasCapture() { return data != nullptr; }
  const char* exportedPath() { return lastExportPath; }
  const char* status() {
    return allocationFailed    ? "allocation_failed"
           : capturing()       ? "capturing"
           : data              ? "frozen"
           : lastExportPath[0] ? "exported"
                               : "idle";
  }

  bool start() {
    if (!settings.dev_mode) return false;
#ifndef LEAF_SIM
    TaskHandle_t expected = nullptr;
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (!captureOwner.compare_exchange_strong(expected, caller) && expected != caller) return false;
#endif
    if (!onCaptureThread()) return false;
    if (capturing()) return false;
    if (!data) {
      // Preserve internal headroom before starting capture.
      // Allocation never occurs in the slot/segment record path.
      void* storage = nullptr;
      if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
          sizeof(Capture) + INTERNAL_RESERVE_BYTES)
        storage = heap_caps_malloc(sizeof(Capture), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!storage) {
        allocationFailed = true;
        Serial.printf(
            "Runtime capture: cannot reserve %u bytes while leaving %u internal bytes free\n",
            (unsigned)sizeof(Capture), (unsigned)INTERNAL_RESERVE_BYTES);
        return false;
      }
      data = new (storage) Capture{};
    } else {
      data->~Capture();
      new (data) Capture{};
    }
    allocationFailed = false;
    lastExportPath[0] = '\0';
    data->startUs = nowUs();
    settingsJson(data->settingsJson, sizeof(data->settingsJson));
    failedAllocations.store(0, std::memory_order_relaxed);
    excludedGps.store(0, std::memory_order_relaxed);
#ifdef LEAF_SIM
    foreignSegments.store(0, std::memory_order_relaxed);
#endif
#ifndef LEAF_SIM
    failedAllocationHook = heap_caps_register_failed_alloc_callback(allocationFailure) == ESP_OK;
#endif
    for (unsigned i = 0; i < 16; ++i) {
      const uint64_t t = workClockUs();
      const uint32_t elapsed = workClockUs() - t;
      if (elapsed > data->clockPairMaxUs) data->clockPairMaxUs = elapsed;
    }
    captureActive = true;
    captureMemory();
    return true;
  }

  void freeze() {
    if (!capturing()) return;
    char currentSettings[400];
    settingsJson(currentSettings, sizeof(currentSettings));
    data->settingsChanged = strcmp(currentSettings, data->settingsJson) != 0;
    captureMemory();
    data->endUs = nowUs();
    data->recorder.close(data->endUs, false);
    captureActive = false;
  }

  void update() {
    const bool requested = settings.dev_mode && settings.diag_cpuUtilization;
    if (!requested) {
      if (capturing()) freeze();
    } else if (!previouslyEnabled)
      start();
    previouslyEnabled = requested;
  }

  void beginSlot(uint8_t index, uint32_t tick, uint64_t expectedUs, uint64_t actualUs) {
    if (!capturing()) return;
    const uint64_t begin = workClockUs();
    data->recorder.begin(index, tick, expectedUs, actualUs);
    const uint32_t elapsed = workClockUs() - begin;
    data->recorder.current.bookkeepingUs = elapsed;
    data->recorder.add(Work::Tasks, elapsed, nowUs());
    if (elapsed > data->recorder.bookkeepingMaxUs) data->recorder.bookkeepingMaxUs = elapsed;
    if (actualUs - lastMemoryUs >= 5000000) captureMemory();
  }

  void tasksFinished(uint8_t buttons, uint8_t displayContext) {
    if (!capturing() || !data->recorder.open) return;
    data->recorder.current.tasksEndUs = nowUs();
    data->recorder.current.buttons |= buttons;
    data->recorder.current.display = displayContext;
  }

  void baroStarting() {
    if (capturing() && data->recorder.open && !data->recorder.current.baroRecorded) {
      data->recorder.current.baroStartDelayUs = late(nowUs(), data->recorder.current.expectedUs);
      data->recorder.current.baroRecorded = true;
    }
  }

  void timerArmed(uint32_t uncertaintyUs) { timerUncertaintyUs = uncertaintyUs; }

  void addWork(Work kind, uint64_t startWorkUs, bool didWork) {
    if (!capturing()) return;
#ifdef LEAF_SIM
    // Host UDP callbacks run on another thread. Never race the loop-owned recorder or
    // mistake receiver-thread CPU time for device-thread work. Disclose incomplete coverage.
    if (!onCaptureThread()) {
      foreignSegments.fetch_add(1, std::memory_order_relaxed);
      return;
    }
#endif
    data->recorder.add(kind, workClockUs() - startWorkUs, nowUs(), didWork);
  }

  uint8_t buttonPinMask(Button button) {
    switch (button) {
      case Button::UP:
        return 1u << 0;
      case Button::DOWN:
        return 1u << 1;
      case Button::LEFT:
        return 1u << 2;
      case Button::RIGHT:
        return 1u << 3;
      case Button::CENTER:
        return 1u << 4;
      default:
        return 0;
    }
  }

  void recordButtonEvent(Button button, ButtonEvent event) {
    if (capturing() && data->recorder.open)
      data->recorder.current.buttonEvents |= buttonPinMask(button);
  }

  void recordDisplay(uint8_t context, uint64_t startWorkUs) {
    if (capturing()) data->recorder.rendered(context, workClockUs() - startWorkUs);
  }

  bool gpsUartBegin(bool prefixMissing) {
    if (!capturing() || !onCaptureThread()) return false;
    data->uart.chunkUs = nowUs();
    data->uart.chunkWorkUs = workClockUs();
    data->uart.chunkPartial = prefixMissing;
    return true;
  }

  void gpsUartEnd(bool payload) {
    if (!capturing() || !onCaptureThread() || !payload) return;
    auto& u = data->uart;
    if (!u.firstReadUs) {
      u.firstReadUs = u.chunkUs;
      u.partial = u.chunkPartial;
    }
    u.readUs += workClockUs() - u.chunkWorkUs;
  }

  void gpsUartDiscard() {
    if (!capturing() || !onCaptureThread()) return;
    data->uart = {};
    data->gps.uartResets++;
  }

  bool gpsBegin(const char* sentence, unsigned length, bool injected) {
    if (!capturing()) return false;
    if (!onCaptureThread()) {
      excludedGps.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    GpsProfile& p = data->gps;
    if (p.active) {
      p.nestedDropped++;
      return false;
    }
    p.current = {};
    auto& s = p.current;
    s.readyUs = nowUs();
    s.firstReadUs = injected || !data->uart.firstReadUs ? s.readyUs : data->uart.firstReadUs;
    s.flags = injected ? Injected : (data->uart.partial ? PartialUart : 0);
    if (!injected) {
      s.stages[0] = data->uart.readUs;
      data->uart = {};
    }
    s.type = gpsType(sentence, length);
    s.length = length;
    if (length >= 3 && sentence[0] == '$')
      for (unsigned i = 0; i < 2; ++i) {
        const char c = sentence[i + 1];
        s.talker[i] = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '?';
      }
    const auto& r = data->recorder;
    if (r.open) {
      s.slot = r.current.index;
      s.tick = r.current.tick;
      s.expectedUs = r.current.expectedUs;
    } else
      s.flags |= NoSlot;
    p.dispatchWorkStart = p.stageWorkStart = workClockUs();
    p.stage = static_cast<uint8_t>(GpsStage::Other);
    p.active = true;
    return true;
  }

  void gpsEnd() {
    if (!gpsProfiling()) return;
    GpsProfile& p = data->gps;
    const uint64_t work = workClockUs();
    p.enter(static_cast<uint8_t>(GpsStage::Other), work);
    p.current.endUs = nowUs();
    p.current.processingUs = p.current.stages[0] + work - p.dispatchWorkStart;
    if (data->recorder.open) {
      if (buttonAffected(data->recorder.current)) p.current.flags |= ButtonSeen;
      if (data->recorder.current.recovery) p.current.flags |= RecoverySeen;
    }
    p.active = false;
    const uint64_t recordStart = workClockUs();
    p.record(p.current);
    const uint32_t elapsed = workClockUs() - recordStart;
    if (elapsed > p.recordMaxUs) p.recordMaxUs = elapsed;
  }

  void gpsValidity(bool accepted) {
    if (gpsProfiling()) data->gps.current.validity = accepted ? 2 : 1;
  }

  uint8_t gpsStageEnter(GpsStage stage) {
    if (!gpsProfiling()) return 255;
    return data->gps.enter(static_cast<uint8_t>(stage), workClockUs());
  }

  void gpsStageExit(uint8_t previous) {
    if (gpsProfiling()) data->gps.enter(previous, workClockUs());
  }

  bool writeJson(Print& out) {
    if (!data || capturing()) return false;
    const Recorder& r = data->recorder;
    out.print("{\"schema_version\":3,\"source\":");
#ifdef LEAF_SIM
    out.printf(
        "\"simulator\",\"work_clock\":\"host_thread_cpu\",\"memory_supported\":false,"
        "\"unmeasured_async_segments\":%lu,",
        (unsigned long)foreignSegments.load());
#else
    out.print("\"device\",\"work_clock\":\"elapsed\",\"memory_supported\":true,");
#endif
    out.print("\"firmware\":");
    stringJson(out, LeafVersionInfo::firmwareVersion());
    out.print(",\"hardware\":");
#ifdef LEAF_SIM
    stringJson(out, LEAF_FIRMWARE_NAME);
#else
    stringJson(out, LeafVersionInfo::hardwareVariant());
#endif
    out.print(",\"capture_memory\":");
    stringJson(out, "internal");
    out.printf(
        ",\"cpu_mhz\":%lu,\"capture_bytes\":%u,\"start_us\":%llu,\"end_us\":%llu,"
        "\"partial_slots\":%lu,\"trace_overwritten\":%lu,\"memory_overwritten\":%lu,"
        "\"clock_pair_max_us\":%lu,\"bookkeeping_max_us\":%lu,"
        "\"memory_sample_max_us\":%lu,\"unassigned_work_us\":%lu,",
        (unsigned long)ESP.getCpuFreqMHz(), (unsigned)sizeof(Capture),
        (unsigned long long)data->startUs, (unsigned long long)data->endUs,
        (unsigned long)r.partialSlots,
        (unsigned long)(r.traceTotal > TRACE_COUNT ? r.traceTotal - TRACE_COUNT : 0),
        (unsigned long)(data->memoryTotal > MEMORY_COUNT ? data->memoryTotal - MEMORY_COUNT : 0),
        (unsigned long)data->clockPairMaxUs, (unsigned long)r.bookkeepingMaxUs,
        (unsigned long)data->memoryMaxUs, (unsigned long)r.unassignedWorkUs);
    out.printf("\"timer_epoch_uncertainty_us\":%lu,\"settings_changed\":%s,\"failed_allocations\":",
               (unsigned long)timerUncertaintyUs, data->settingsChanged ? "true" : "false");
    if (failedAllocationHook)
      out.printf("%lu", (unsigned long)failedAllocations.load());
    else
      out.print("null");
    out.print(",\"settings\":");
    out.print(data->settingsJson);
    out.print(",\"histogram_upper_us\":[");
    for (unsigned i = 0; i < HISTOGRAM_COUNT; ++i)
      out.printf("%s%lu", i ? "," : "", (unsigned long)HISTOGRAM_UPPER_US[i]);
    out.print("],\"slots\":[");
    for (unsigned i = 0; i < SLOT_COUNT; ++i) {
      const Stats& s = r.stats[i];
      out.printf(
          "%s{\"slot\":%u,\"label\":\"%s\",\"count\":%lu,\"deadline_misses\":%lu,"
          "\"duration_overruns\":%lu,\"skipped_ticks\":%lu,\"max_occupied_us\":%lu,"
          "\"max_start_delay_us\":%lu,\"max_end_delay_us\":%lu,\"max_baro_start_delay_us\":%lu,"
          "\"tasks_us\":%llu,\"gps_us\":%llu,\"services_us\":%llu,\"poll_us\":%llu,\"histogram\":[",
          i ? "," : "", i, slotLabel(i), (unsigned long)s.count, (unsigned long)s.deadlineMisses,
          (unsigned long)s.durationOverruns, (unsigned long)s.skippedTicks,
          (unsigned long)s.maxOccupiedUs, (unsigned long)s.maxStartDelayUs,
          (unsigned long)s.maxEndDelayUs, (unsigned long)s.maxBaroStartDelayUs,
          (unsigned long long)s.workUs[0], (unsigned long long)s.workUs[1],
          (unsigned long long)s.workUs[2], (unsigned long long)s.workUs[3]);
      for (unsigned j = 0; j < HISTOGRAM_COUNT; ++j)
        out.printf("%s%lu", j ? "," : "", (unsigned long)s.histogram[j]);
      const Excluded& e = r.excluded[i];
      out.printf(
          "],\"button_excluded\":%lu,\"recovery_excluded\":%lu,"
          "\"recovery_deadline_misses\":%lu,\"recovery_skipped_ticks\":%lu,"
          "\"max_recovery_start_delay_us\":%lu,\"max_recovery_end_delay_us\":%lu,"
          "\"max_recovery_baro_start_delay_us\":%lu,\"max_recovery_occupied_us\":%lu}",
          (unsigned long)e.buttons, (unsigned long)e.recovery, (unsigned long)e.recoveryMisses,
          (unsigned long)e.recoverySkipped, (unsigned long)e.maxRecoveryStartUs,
          (unsigned long)e.maxRecoveryEndUs, (unsigned long)e.maxRecoveryBaroUs,
          (unsigned long)e.maxRecoveryOccupiedUs);
    }
    out.print("],\"pages\":[");
    static constexpr const char* pageNames[PAGE_COUNT] = {
        "Basic", "User", "Thermal Core", "Navigate", "Debug", "Debug 2", "Thermal Track"};
    for (unsigned i = 0; i < PAGE_COUNT; ++i) {
      const Page& p = r.pages[i];
      out.printf(
          "%s{\"context\":%u,\"label\":\"%s\",\"count\":%lu,\"total_us\":%llu,"
          "\"max_render_us\":%lu,\"slots\":%lu,\"max_slot_occupied_us\":%lu,"
          "\"deadline_misses\":%lu,\"button_excluded\":%lu,\"recovery_excluded\":%lu,\"histogram\":"
          "[",
          i ? "," : "", i + 10, pageNames[i], (unsigned long)p.frames.count,
          (unsigned long long)p.frames.totalUs, (unsigned long)p.frames.maxUs,
          (unsigned long)p.slots, (unsigned long)p.maxSlotUs, (unsigned long)p.deadlineMisses,
          (unsigned long)p.buttonFrames, (unsigned long)p.recoveryFrames);
      for (unsigned j = 0; j < HISTOGRAM_COUNT; ++j)
        out.printf("%s%lu", j ? "," : "", (unsigned long)p.frames.histogram[j]);
      out.print("]}");
    }
    out.print("],\"button_slots\":");
    totalsJson(out, r.buttonStats);
    out.print(",\"recovery_slots\":");
    totalsJson(out, r.recoveryStats);
    out.print(",\"worst_recovery_previous\":");
    slotJson(out, r.recoveryPrevious);
    out.print(",\"worst_recovery\":");
    slotJson(out, r.worstRecovery);
    out.print(",\"worst_previous\":");
    slotJson(out, r.worstPrevious);
    out.print(",\"worst\":");
    slotJson(out, r.worst);
    out.print(",\"trace\":[");
    const unsigned traceCount = r.traceTotal < TRACE_COUNT ? r.traceTotal : TRACE_COUNT;
    for (unsigned i = 0; i < traceCount; ++i) {
      if (i) out.print(',');
      slotJson(out, r.trace[(r.traceTotal - traceCount + i) % TRACE_COUNT]);
    }
    out.print("],\"gps_profile\":");
    gpsJson(out, data->gps);
    out.printf(",\"trace_capacity\":%u,\"memory_capacity\":%u", TRACE_COUNT, MEMORY_COUNT);
    out.print(",\"first_memory\":");
    if (data->memoryTotal)
      memoryJson(out, data->firstMemory);
    else
      out.print("null");
    out.print(",\"memory_minima\":");
    if (data->memoryTotal)
      memoryJson(out, data->memoryMinima);
    else
      out.print("null");
    out.print(",\"memory\":[");
    const unsigned memoryCount =
        data->memoryTotal < MEMORY_COUNT ? data->memoryTotal : MEMORY_COUNT;
    for (unsigned i = 0; i < memoryCount; ++i) {
      if (i) out.print(',');
      memoryJson(out, data->memory[(data->memoryTotal - memoryCount + i) % MEMORY_COUNT]);
    }
    out.println("]}");
    return true;
  }

  bool exportToSd() {
    if (!data || captureActive || !diagnostic_logs::ensureDirectory()) return false;
    char path[80];
    // Never overwrite earlier boot captures. Export is deliberately outside measurement.
    do {
      snprintf(path, sizeof(path), "/diagnostics/runtime_%lu.json",
               (unsigned long)++exportSequence);
    } while (SD_MMC.exists(path));
    File file = SD_MMC.open(path, "w", true);
    if (!file) return false;
    BufferedFilePrint output(file);
    const bool ok = writeJson(output);
    output.flushBuffer();
    file.flush();
    const bool written = ok && !output.failed() && file.size() > 0;
    file.close();
    if (written) snprintf(lastExportPath, sizeof(lastExportPath), "%s", path);
    Serial.printf("Runtime capture: %s (%s)\n", path, written ? "exported" : "write failed");
    if (written) {
      data->~Capture();
      heap_caps_free(data);
      data = nullptr;
    }
    return written;
  }
}  // namespace cpu_utilization
