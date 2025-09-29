#include "file_writer.h"

RingbufHandle_t AsyncLogger::rb = nullptr;
TaskHandle_t AsyncLogger::task = nullptr;
size_t AsyncLogger::freeLowWaterMark =
    0;  // Size in bytes of available space in the buffer, low watermark
size_t AsyncLogger::bufferSize = 0;  // Size of the ring buffer

bool AsyncLogger::begin(size_t buffer_size, uint32_t task_stack, UBaseType_t task_prio) {
  if (rb != nullptr) {
    return false;  // already started
  }

  AsyncLogger::bufferSize = buffer_size;

  rb = xRingbufferCreate(buffer_size, RINGBUF_TYPE_NOSPLIT);
  if (!rb) {
    return false;
  }

  // Clear/Initialize the available low watermark
  getFreeSizeLowWatermark();
  BaseType_t res = xTaskCreate(writerTask, "AsyncLogWriter", task_stack, nullptr, task_prio, &task);

  return (res == pdPASS);
}

bool AsyncLogger::enqueue(File* file, const char* data, uint16_t len, TickType_t timeout) {
  if (!rb || !file || len == 0) {
    return false;
  }

  size_t total_size = sizeof(LogMsgHeader) + len;
  void* buf = nullptr;
  if (xRingbufferSendAcquire(rb, &buf, total_size, timeout) != pdTRUE || !buf) {
    return false;  // buffer full or timeout
  }

  LogMsgHeader* msg = (LogMsgHeader*)buf;
  msg->file = file;
  msg->len = len;
  memcpy((msg + 1), data, len);

  xRingbufferSendComplete(rb, buf);
  return true;
}

bool AsyncLogger::enqueuef(File* file, const char* format, ...) {
  if (!file || !format) {
    return false;
  }

  // Temporary buffer on stack for formatting
  char buffer[256];  // adjust size as needed
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (len < 0) {
    return false;  // formatting error
  }

  // If the output was truncated, clamp to buffer size
  if (len >= (int)sizeof(buffer)) {
    len = sizeof(buffer) - 1;
  }

  return enqueue(file, buffer, len, pdMS_TO_TICKS(100));  // or pass timeout differently
}

void AsyncLogger::end() {
  if (task) {
    // Wait until the write-buffer has been emptied
    flush();

    // Remove the task writer
    vTaskDelete(task);
    task = nullptr;
  }
  if (rb) {
    vRingbufferDelete(rb);
    rb = nullptr;
  }
}

void AsyncLogger::flush() {
  // Wait until the write-buffer has been emptied
  UBaseType_t itemsWaiting;

  while (true) {
    vRingbufferGetInfo(rb, nullptr, nullptr, nullptr, nullptr, &itemsWaiting);
    if (itemsWaiting == 0) break;
    Serial.println(itemsWaiting);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

size_t AsyncLogger::getFreeSizeLowWatermark() {
  // Short circuit if not running
  if (rb == nullptr) {
    return 0;
  }

  auto ret = freeLowWaterMark;
  freeLowWaterMark = xRingbufferGetCurFreeSize(rb);
  return ret;
}

void AsyncLogger::writerTask(void* arg) {
  while (true) {
    size_t item_len;
    // Get a messages header from the ring buffer.  Lock up to the header
    LogMsgHeader* msg = (LogMsgHeader*)xRingbufferReceive(rb, &item_len, portMAX_DELAY);
    if (msg) {
      auto header = (LogMsgHeader*)msg;
      char* payload = (char*)(header + 1);

      // Write the payload (just after the header) to the SD card.
      if (header->file) {
        header->file->write((uint8_t*)payload, header->len);
      }

      // Update the low watermark
      auto freeSize = xRingbufferGetCurFreeSize(rb);
      if (freeSize < freeLowWaterMark) {
        freeLowWaterMark = freeSize;
      }

      // Flag the header as being consumed now, freeing it from the ring buffer.
      vRingbufferReturnItem(rb, msg);
    }
  }
}
