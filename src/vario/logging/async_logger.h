#pragma once
#include <Arduino.h>
#include <FS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

// Message header stored in ringbuffer
struct LogMsgHeader {
  File* file;    // pointer to FS::File to write into
  uint16_t len;  // payload length
                 // payload follows immediately after header
};

/// @brief Asynchronously writes data to a File on the SD card.  This is done
// on a separate thread as to not block the writing steps.
// As of 2025-09-29, the largest block I saw was ~70 bytes, meaning we can queue
// ~58 messages of max length
class AsyncLogger {
 public:
  // Create logger with given buffer size (bytes) and stack size for writer task
  static bool begin(size_t buffer_size = 4096, uint32_t task_stack = 4096,
                    UBaseType_t task_prio = 1);

  // Enqueue a log message (returns false if buffer full)
  static bool enqueue(File* file, const char* data, uint16_t len, TickType_t timeout = 0);
  static bool enqueuef(File* file, const char* format, ...);

  // Optionally call to stop task and clean up
  static void end();

  // Blocks until the queue is flushed
  static void flush();

  // Gets and clears the size low watermark.
  static size_t getFreeSizeLowWatermark();

  /// @brief Gets the high watermark of how long it took to write to the SD card.  Clears on read
  /// @return SD card write duration high watermark in uS.
  static unsigned long getWriteTimeHighWatermarkUs() {
    auto ret = writeHighWatermarkUs;
    writeHighWatermarkUs = 0;
    return ret;
  }

  // Returns the number of messages that have been dropped due to the queue being full
  static uint32_t getDropped() { return droppedEntries; }

 private:
  static void writerTask(void* arg);
  static RingbufHandle_t rb;
  static TaskHandle_t task;
  static size_t freeLowWaterMark;  // Size in bytes of available space in the buffer, low watermark
  static size_t bufferSize;        // Size of the ring buffer
  static uint32_t droppedEntries;  // Number of log messages dropped (buffer was full)
  static unsigned long
      writeHighWatermarkUs;  // High watermark of how long it took to write to the SD card.
};
