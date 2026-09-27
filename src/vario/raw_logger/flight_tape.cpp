#include "raw_logger/flight_tape.h"

#include <cstring>

namespace leaf::raw {

  FlightTape flightTape;

  namespace {
    constexpr TickType_t WRITER_IDLE_TICKS = pdMS_TO_TICKS(100);
    constexpr uint32_t WRITER_STACK_BYTES = 4096;

    void copyString(char* destination, size_t capacity, const char* source) {
      if (!capacity) return;
      if (!source) source = "";
      strncpy(destination, source, capacity - 1);
      destination[capacity - 1] = '\0';
    }
  }  // namespace

  uint32_t FlightTape::crc32(const void* data, size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
      crc ^= bytes[i];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint32_t mask = -(crc & 1U);
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
      }
    }
    return ~crc;
  }

  bool FlightTape::begin() {
    if (writerTask_) return true;
    return xTaskCreatePinnedToCore(writerTaskEntry, "raw_sd_writer", WRITER_STACK_BYTES, this, 2,
                                   &writerTask_, 0) == pdPASS;
  }

  bool FlightTape::start(const char* path, const StartMetadata& metadata) {
    if (!writerTask_ && !begin()) return false;
    if (recording() || writerBusy_.load(std::memory_order_acquire) ||
        readyHead_.load(std::memory_order_acquire) != readyTail_.load(std::memory_order_acquire)) {
      return false;
    }

    file_ = SD_MMC.open(path, FILE_WRITE, true);
    if (!file_) return false;

    FileHeader header{};
    memcpy(header.magic, "LEAFRAW\0", sizeof(header.magic));
    header.formatVersion = FORMAT_VERSION;
    header.headerBytes = FILE_HEADER_BYTES;
    header.chunkBytes = CHUNK_BYTES;
    copyString(header.hardware, sizeof(header.hardware), metadata.hardware);
    copyString(header.firmware, sizeof(header.firmware), metadata.firmware);
    copyString(header.gitRevision, sizeof(header.gitRevision), metadata.gitRevision);
    memcpy(header.deviceMac, metadata.deviceMac, sizeof(header.deviceMac));
    header.startUtcUs = metadata.startUtcUs;
    header.startMonotonicUs = metadata.startMonotonicUs;
    header.timebaseHz = 1000000;
    header.sensorMask = metadata.sensorMask;
    memcpy(header.baroCalibration, metadata.baroCalibration, sizeof(header.baroCalibration));
    header.imuRateHz = metadata.imuRateHz;
    header.baroRateHz = metadata.baroRateHz;
    header.headerCrc32 = 0;
    header.headerCrc32 = crc32(&header, sizeof(header));

    if (file_.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
      file_.close();
      return false;
    }
    file_.flush();

    copyString(path_, sizeof(path_), path);
    active_ = false;
    activeOffset_ = 0;
    activeRecordCount_ = 0;
    nextSequence_ = 0;
    readyHead_.store(0, std::memory_order_release);
    readyTail_.store(0, std::memory_order_release);
    writerBusy_.store(false, std::memory_order_release);
    writeErrors_.store(0, std::memory_order_release);
    bytesWritten_.store(sizeof(header), std::memory_order_release);
    chunksWritten_.store(0, std::memory_order_release);
    recordsWritten_.store(0, std::memory_order_release);
    droppedRecords_.store(0, std::memory_order_release);
    droppedBytes_.store(0, std::memory_order_release);
    highWaterBytes_.store(0, std::memory_order_release);
    maxWriteLatencyMs_.store(0, std::memory_order_release);
    reportedDroppedRecords_ = 0;
    recording_.store(true, std::memory_order_release);
    return true;
  }

  bool FlightTape::stop(uint32_t timeoutMs) {
    const bool wasRecording = recording_.exchange(false, std::memory_order_acq_rel);
    if (!wasRecording && !file_) return true;
    if (wasRecording) sealChunk();
    if (writerTask_) xTaskNotifyGive(writerTask_);

    const uint32_t started = millis();
    while (
        (readyHead_.load(std::memory_order_acquire) != readyTail_.load(std::memory_order_acquire) ||
         writerBusy_.load(std::memory_order_acquire)) &&
        millis() - started < timeoutMs) {
      delay(5);
    }

    const bool drained =
        readyHead_.load(std::memory_order_acquire) == readyTail_.load(std::memory_order_acquire) &&
        !writerBusy_.load(std::memory_order_acquire);
    if (drained && file_) {
      file_.flush();
      file_.close();
    }
    return drained && healthy();
  }

  bool FlightTape::beginChunk(uint64_t timestampUs) {
    const uint32_t head = readyHead_.load(std::memory_order_relaxed);
    const uint32_t tail = readyTail_.load(std::memory_order_acquire);
    if (head - tail >= CHUNK_COUNT) return false;

    ChunkSlot& slot = slots_[head % CHUNK_COUNT];
    memset(slot.bytes, 0xFF, sizeof(slot.bytes));
    auto* header = reinterpret_cast<ChunkHeader*>(slot.bytes);
    memcpy(header->magic, "LRCH", sizeof(header->magic));
    header->sequence = nextSequence_++;
    header->baseMonotonicUs = timestampUs;
    header->payloadBytes = 0;
    header->recordCount = 0;
    header->crc32 = 0;
    activeOffset_ = sizeof(ChunkHeader);
    activeRecordCount_ = 0;
    activeBaseUs_ = timestampUs;
    active_ = true;
    return true;
  }

  bool FlightTape::sealChunk() {
    if (!active_) return true;
    const uint32_t head = readyHead_.load(std::memory_order_relaxed);
    ChunkSlot& slot = slots_[head % CHUNK_COUNT];
    auto* header = reinterpret_cast<ChunkHeader*>(slot.bytes);
    header->payloadBytes = activeOffset_ - sizeof(ChunkHeader);
    header->recordCount = activeRecordCount_;
    header->crc32 = 0;
    header->crc32 = crc32(slot.bytes, sizeof(slot.bytes));

    active_ = false;
    activeOffset_ = 0;
    activeRecordCount_ = 0;
    readyHead_.store(head + 1, std::memory_order_release);
    if (writerTask_) xTaskNotifyGive(writerTask_);
    return true;
  }

  void FlightTape::noteDrop(uint16_t payloadBytes) {
    droppedRecords_.fetch_add(1, std::memory_order_relaxed);
    droppedBytes_.fetch_add(sizeof(RecordHeader) + payloadBytes, std::memory_order_relaxed);
  }

  void FlightTape::maybeRecordDataLoss(uint64_t timestampUs) {
    const uint32_t dropped = droppedRecords_.load(std::memory_order_relaxed);
    if (dropped == reportedDroppedRecords_) return;
    const DataLossPayload payload{dropped, droppedBytes_.load(std::memory_order_relaxed)};
    if (appendInternal(RecordType::DataLoss, &payload, sizeof(payload), timestampUs, 0)) {
      reportedDroppedRecords_ = dropped;
    }
  }

  bool FlightTape::appendBytes(RecordType type, const void* payload, uint16_t payloadBytes,
                               uint64_t timestampUs, uint8_t flags) {
    if (!recording()) return false;
    if (type != RecordType::DataLoss) maybeRecordDataLoss(timestampUs);
    if (appendInternal(type, payload, payloadBytes, timestampUs, flags)) return true;
    noteDrop(payloadBytes);
    return false;
  }

  bool FlightTape::appendInternal(RecordType type, const void* payload, uint16_t payloadBytes,
                                  uint64_t timestampUs, uint8_t flags) {
    const size_t recordBytes = sizeof(RecordHeader) + payloadBytes;
    if (recordBytes > CHUNK_BYTES - sizeof(ChunkHeader)) return false;
    if (!active_ && !beginChunk(timestampUs)) return false;

    uint64_t delta = timestampUs - activeBaseUs_;
    if (activeOffset_ + recordBytes > CHUNK_BYTES || delta > UINT32_MAX) {
      sealChunk();
      if (!beginChunk(timestampUs)) return false;
      delta = 0;
    }

    const uint32_t head = readyHead_.load(std::memory_order_relaxed);
    ChunkSlot& slot = slots_[head % CHUNK_COUNT];
    auto* record = reinterpret_cast<RecordHeader*>(slot.bytes + activeOffset_);
    record->type = static_cast<uint8_t>(type);
    record->flags = flags;
    record->payloadBytes = payloadBytes;
    record->deltaUs = static_cast<uint32_t>(delta);
    if (payloadBytes && payload) memcpy(record + 1, payload, payloadBytes);
    activeOffset_ += recordBytes;
    activeRecordCount_++;
    recordsWritten_.fetch_add(1, std::memory_order_relaxed);

    const uint32_t used = bufferedBytes();
    uint32_t previous = highWaterBytes_.load(std::memory_order_relaxed);
    while (used > previous &&
           !highWaterBytes_.compare_exchange_weak(previous, used, std::memory_order_relaxed)) {
    }
    return true;
  }

  uint32_t FlightTape::bufferedBytes() const {
    const uint32_t ready =
        readyHead_.load(std::memory_order_acquire) - readyTail_.load(std::memory_order_acquire);
    return ready * CHUNK_BYTES + (active_ ? activeOffset_ : 0);
  }

  void FlightTape::service(uint64_t nowUs) {
    if (!recording()) return;
    maybeRecordDataLoss(nowUs);
    if (active_ && nowUs - activeBaseUs_ >= CHUNK_MAX_AGE_US) sealChunk();
  }

  Statistics FlightTape::statistics() const {
    return Statistics{bytesWritten_.load(std::memory_order_relaxed),
                      chunksWritten_.load(std::memory_order_relaxed),
                      recordsWritten_.load(std::memory_order_relaxed),
                      droppedRecords_.load(std::memory_order_relaxed),
                      droppedBytes_.load(std::memory_order_relaxed),
                      highWaterBytes_.load(std::memory_order_relaxed),
                      maxWriteLatencyMs_.load(std::memory_order_relaxed),
                      writeErrors_.load(std::memory_order_relaxed)};
  }

  void FlightTape::writerTaskEntry(void* context) {
    static_cast<FlightTape*>(context)->writerLoop();
  }

  void FlightTape::writerLoop() {
    uint32_t lastFlushMs = millis();
    while (true) {
      uint32_t tail = readyTail_.load(std::memory_order_relaxed);
      const uint32_t head = readyHead_.load(std::memory_order_acquire);
      if (tail == head) {
        ulTaskNotifyTake(pdTRUE, WRITER_IDLE_TICKS);
        continue;
      }

      writerBusy_.store(true, std::memory_order_release);
      ChunkSlot& slot = slots_[tail % CHUNK_COUNT];
      const uint32_t started = millis();
      const size_t written = file_ ? file_.write(slot.bytes, sizeof(slot.bytes)) : 0;
      if (written == sizeof(slot.bytes) && millis() - lastFlushMs >= 1000) {
        file_.flush();
        lastFlushMs = millis();
      }
      const uint32_t elapsed = millis() - started;
      uint32_t previous = maxWriteLatencyMs_.load(std::memory_order_relaxed);
      while (elapsed > previous && !maxWriteLatencyMs_.compare_exchange_weak(
                                       previous, elapsed, std::memory_order_relaxed)) {
      }
      if (written == sizeof(slot.bytes)) {
        bytesWritten_.fetch_add(written, std::memory_order_relaxed);
        chunksWritten_.fetch_add(1, std::memory_order_relaxed);
      } else {
        writeErrors_.fetch_add(1, std::memory_order_relaxed);
      }
      readyTail_.store(++tail, std::memory_order_release);
      writerBusy_.store(false, std::memory_order_release);
    }
  }

}  // namespace leaf::raw
