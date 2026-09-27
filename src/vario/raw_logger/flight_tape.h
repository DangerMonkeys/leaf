#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace leaf::raw {

  constexpr uint16_t FORMAT_VERSION = 1;
  constexpr size_t FILE_HEADER_BYTES = 512;
  constexpr size_t CHUNK_BYTES = 4096;
  constexpr size_t CHUNK_COUNT = 16;
  constexpr size_t BUFFER_BYTES = CHUNK_BYTES * CHUNK_COUNT;
  constexpr uint32_t CHUNK_MAX_AGE_US = 250000;

  enum class RecordType : uint8_t {
    RawBaroPressure = 1,
    RawBaroTemperature = 2,
    RawImuAccel = 3,
    RawImuGyro = 4,
    RawImuMag = 5,
    RawImuQuaternion = 6,
    RawGpsNmea = 7,
    RawAmbient = 8,
    RawPower = 9,
    TimeSync = 10,
    Event = 11,
    DataLoss = 12,

    BusPressure = 64,
    BusMotion = 65,
    BusAmbient = 66,
  };

  enum SensorMask : uint32_t {
    SensorBarometer = 1U << 0,
    SensorImu = 1U << 1,
    SensorGps = 1U << 2,
    SensorAmbient = 1U << 3,
    SensorPower = 1U << 4,
  };

#pragma pack(push, 1)
  struct FileHeader {
    char magic[8];
    uint16_t formatVersion;
    uint16_t headerBytes;
    uint16_t chunkBytes;
    uint16_t flags;
    char hardware[16];
    char firmware[32];
    char gitRevision[41];
    uint8_t deviceMac[6];
    uint8_t alignmentPad;
    int64_t startUtcUs;
    uint64_t startMonotonicUs;
    uint32_t timebaseHz;
    uint32_t sensorMask;
    uint16_t baroCalibration[6];
    uint16_t imuRateHz;
    uint16_t baroRateHz;
    uint32_t headerCrc32;
    uint8_t reserved[356];
  };

  struct ChunkHeader {
    char magic[4];
    uint32_t sequence;
    uint64_t baseMonotonicUs;
    uint16_t payloadBytes;
    uint16_t recordCount;
    uint32_t crc32;
  };

  struct RecordHeader {
    uint8_t type;
    uint8_t flags;
    uint16_t payloadBytes;
    uint32_t deltaUs;
  };

  struct RawScalarPayload {
    uint32_t value;
  };

  struct RawVectorPayload {
    int16_t x;
    int16_t y;
    int16_t z;
  };

  struct RawGyroPayload {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t biasX;
    int16_t biasY;
    int16_t biasZ;
  };

  struct RawQuaternionPayload {
    int32_t q1;
    int32_t q2;
    int32_t q3;
    int16_t accuracy;
  };

  struct RawAmbientPayload {
    uint32_t humidity;
    uint32_t temperature;
  };

  struct RawPowerPayload {
    uint16_t batteryMv;
    uint16_t chargeCurrentMa;
    uint8_t charging;
    uint8_t reserved[3];
  };

  struct TimeSyncPayload {
    int64_t utcUs;
    uint8_t fixQuality;
    uint8_t satellites;
    uint8_t reserved[6];
  };

  struct DataLossPayload {
    uint32_t droppedRecords;
    uint32_t droppedBytes;
  };

  struct BusPressurePayload {
    int32_t pressure;
  };

  struct BusMotionPayload {
    uint8_t hasAcceleration;
    uint8_t hasOrientation;
    uint8_t reserved[2];
    double ax;
    double ay;
    double az;
    double qx;
    double qy;
    double qz;
  };

  struct BusAmbientPayload {
    float temperature;
    float relativeHumidity;
  };
#pragma pack(pop)

  static_assert(sizeof(FileHeader) == FILE_HEADER_BYTES);
  static_assert(sizeof(ChunkHeader) == 24);
  static_assert(sizeof(RecordHeader) == 8);
  static_assert(sizeof(BusMotionPayload) == 52);

  struct StartMetadata {
    const char* hardware = nullptr;
    const char* firmware = nullptr;
    const char* gitRevision = nullptr;
    uint8_t deviceMac[6] = {};
    int64_t startUtcUs = 0;
    uint64_t startMonotonicUs = 0;
    uint32_t sensorMask = 0;
    uint16_t baroCalibration[6] = {};
    uint16_t imuRateHz = 0;
    uint16_t baroRateHz = 0;
  };

  struct Statistics {
    uint64_t bytesWritten = 0;
    uint32_t chunksWritten = 0;
    uint32_t recordsWritten = 0;
    uint32_t droppedRecords = 0;
    uint32_t droppedBytes = 0;
    uint32_t bufferHighWaterBytes = 0;
    uint32_t maxWriteLatencyMs = 0;
    uint32_t writeErrors = 0;
  };

  class FlightTape {
   public:
    bool begin();
    bool start(const char* path, const StartMetadata& metadata);
    bool stop(uint32_t timeoutMs = 5000);

    bool appendBytes(RecordType type, const void* payload, uint16_t payloadBytes,
                     uint64_t timestampUs, uint8_t flags = 0);

    template <typename T>
    bool append(RecordType type, const T& payload, uint64_t timestampUs, uint8_t flags = 0) {
      return appendBytes(type, &payload, sizeof(payload), timestampUs, flags);
    }

    void service(uint64_t nowUs);

    bool recording() const { return recording_.load(std::memory_order_acquire); }
    bool healthy() const { return writeErrors_.load(std::memory_order_acquire) == 0; }
    Statistics statistics() const;
    const char* path() const { return path_; }

    static uint32_t crc32(const void* data, size_t length);

   private:
    struct alignas(4) ChunkSlot {
      uint8_t bytes[CHUNK_BYTES];
    };

    static void writerTaskEntry(void* context);
    void writerLoop();
    bool appendInternal(RecordType type, const void* payload, uint16_t payloadBytes,
                        uint64_t timestampUs, uint8_t flags);
    bool beginChunk(uint64_t timestampUs);
    bool sealChunk();
    void noteDrop(uint16_t payloadBytes);
    void maybeRecordDataLoss(uint64_t timestampUs);
    uint32_t bufferedBytes() const;

    alignas(4) ChunkSlot slots_[CHUNK_COUNT];
    std::atomic<uint32_t> readyHead_{0};
    std::atomic<uint32_t> readyTail_{0};
    uint32_t activeOffset_ = 0;
    uint16_t activeRecordCount_ = 0;
    uint64_t activeBaseUs_ = 0;
    bool active_ = false;
    uint32_t nextSequence_ = 0;

    File file_;
    TaskHandle_t writerTask_ = nullptr;
    std::atomic<bool> writerBusy_{false};
    std::atomic<bool> recording_{false};
    std::atomic<uint32_t> writeErrors_{0};

    std::atomic<uint64_t> bytesWritten_{0};
    std::atomic<uint32_t> chunksWritten_{0};
    std::atomic<uint32_t> recordsWritten_{0};
    std::atomic<uint32_t> droppedRecords_{0};
    std::atomic<uint32_t> droppedBytes_{0};
    std::atomic<uint32_t> highWaterBytes_{0};
    std::atomic<uint32_t> maxWriteLatencyMs_{0};
    uint32_t reportedDroppedRecords_ = 0;
    char path_[96] = {};
  };

  extern FlightTape flightTape;

}  // namespace leaf::raw
