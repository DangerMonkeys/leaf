#include "logging/fanet_log.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <atomic>
#include "comms/fanet_radio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "instruments/gps.h"
#include "logbook/logbook_io.h"
#include "storage/sd_card.h"
#include "system/version_info.h"
#include "ui/settings/settings.h"

namespace fanet_log {
  namespace {
    constexpr unsigned QUEUE_LENGTH = 32;
    struct QueuedObservation {
      uint32_t session;
      Observation observation;
    };
    // Allocated on first use and retained: RX may still hold an old session when STOP runs.
    QueueHandle_t queue = nullptr;
    std::atomic<uint32_t> activeSession{0};
    std::atomic<uint32_t> dropped{0};
    uint32_t nextSession = 0;
    uint32_t fileSequence = 0;
    uint32_t flightStartMs = 0;
    uint32_t droppedAtStart = 0;
    bool flightActive = false;
    bool failed = false;
    char path[80] = {};

    bool drain(uint32_t session) {
      if (!path[0] || !sdcard.firmwareCanAccessFilesystem()) return false;
      if (uxQueueMessagesWaiting(queue) == 0) return true;
      // Close after every batch so card removal/USB handoff cannot leave a retained File.
      File file = SD_MMC.open(path, FILE_APPEND);
      if (!file) return false;
      bool ok = true;
      QueuedObservation entry;
      for (unsigned i = 0; i < QUEUE_LENGTH && xQueueReceive(queue, &entry, 0) == pdTRUE; ++i) {
        if (entry.session != session) continue;
        const auto& o = entry.observation;
        char line[240];
        char position[90] = ",,";
        if (o.receiverFix)
          snprintf(position, sizeof(position), "%.3f,%.8f,%.8f", o.distanceKm, o.receiverLat,
                   o.receiverLon);
        const int length =
            snprintf(line, sizeof(line), "%lu,%06lX,%.2f,%.2f,%.8f,%.8f,%s,%lu\n",
                     (unsigned long)(o.receivedMs - flightStartMs), (unsigned long)o.neighborId,
                     o.rssi, o.snr, o.neighborLat, o.neighborLon, position,
                     (unsigned long)(dropped.load(std::memory_order_relaxed) - droppedAtStart));
        if (length <= 0 || length >= sizeof(line) ||
            file.write(reinterpret_cast<const uint8_t*>(line), length) != length) {
          ok = false;
          break;
        }
      }
      file.flush();
      file.close();
      return ok;
    }

    void stop() {
      const uint32_t session = activeSession.exchange(0, std::memory_order_acq_rel);
      if (session) {
        logbook_io::Guard transaction(true);
        if (!drain(session)) Serial.println("FANET log: final write failed");
      }
      path[0] = '\0';
    }

    bool start() {
      if (!sdcard.firmwareCanAccessFilesystem()) return false;
      if (!queue) queue = xQueueCreate(QUEUE_LENGTH, sizeof(QueuedObservation));
      if (!queue) return false;
      QueuedObservation discarded;
      while (xQueueReceive(queue, &discarded, 0) == pdTRUE) {
      }
      if (!SD_MMC.exists("/fanetlogs") && !SD_MMC.mkdir("/fanetlogs")) return false;
      tm cal;
      char date[32] = "unsynced";
      if (gps.getUtcDateTime(cal)) strftime(date, sizeof(date), "%Y-%m-%d_%H%M%S", &cal);
      do {
        snprintf(path, sizeof(path), "/fanetlogs/FANET_%s_%lu.csv", date,
                 (unsigned long)++fileSequence);
      } while (SD_MMC.exists(path));
      File file = SD_MMC.open(path, FILE_WRITE);
      if (!file) return false;
      char header[400];
      const int length =
          snprintf(header, sizeof(header),
                   "# firmware,%s\n# receiver_id,%s\n# region,%u\n# forwarding,%u\n"
                   "elapsed_ms,neighbor_id,rssi_dbm,snr_db,neighbor_lat,neighbor_lon,"
                   "distance_km,receiver_lat,receiver_lon,queue_drops\n",
                   LeafVersionInfo::firmwareVersion(), FanetRadio::getAddress().c_str(),
                   static_cast<unsigned>(settings.fanet_region),
                   static_cast<unsigned>(settings.dev_fanetFwd));
      const bool ok = length > 0 && length < sizeof(header) &&
                      file.write(reinterpret_cast<const uint8_t*>(header), length) == length;
      file.flush();
      file.close();
      if (!ok) return false;
      droppedAtStart = dropped.load(std::memory_order_relaxed);
      if (++nextSession == 0) ++nextSession;
      activeSession.store(nextSession, std::memory_order_release);
      Serial.printf("FANET log: %s\n", path);
      return true;
    }
  }  // namespace

  bool recording() { return activeSession.load(std::memory_order_acquire) != 0; }

  void record(const Observation& observation) {
    const uint32_t session = activeSession.load(std::memory_order_acquire);
    if (!session) return;
    const QueuedObservation entry{session, observation};
    if (xQueueSend(queue, &entry, 0) != pdTRUE) dropped.fetch_add(1, std::memory_order_relaxed);
  }

  void beginFlight() {
    stop();
    flightActive = true;
    flightStartMs = millis();
    failed = false;
    update();
  }

  void endFlight() {
    flightActive = false;
    stop();
    failed = false;
  }

  bool update() {
    if (!flightActive || !settings.dev_mode || !settings.dev_logFanet || !fanetRadio.isDetected()) {
      stop();
      failed = false;
      return true;
    }
    if (failed) return false;
    // Share the SD ownership boundary, deferring a periodic batch if another user is busy.
    logbook_io::Guard transaction;
    if (!transaction) return true;
    if ((!recording() && !start()) || !drain(activeSession.load(std::memory_order_acquire))) {
      activeSession.store(0, std::memory_order_release);
      path[0] = '\0';
      failed = true;
      Serial.println("FANET log: write/open failed; toggle Log FANET to retry");
      return false;
    }
    return true;
  }
}  // namespace fanet_log
