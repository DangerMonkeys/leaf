// FreeRTOS primitives on host threads.
//
// Almost all firmware code runs on the single device thread, so these mostly serve the message
// bus's recursive mutex and the heap monitor's task bookkeeping.  Tasks that the firmware does
// spawn become host threads whose delays consume virtual time, so they stay in step with the
// device loop rather than racing ahead in real time.

#include <freertos/FreeRTOS.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sim/clock.h"

namespace {

  struct SimMutex {
    std::recursive_timed_mutex mutex;
    bool binary = false;
    std::atomic<int> count{0};
  };

  struct SimQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> items;
    size_t itemSize = 0;
    size_t capacity = 0;
  };

  struct SimTask {
    std::thread thread;
    std::string name;
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> notifyValue{0};
  };

  struct SimTimer {
    std::string name;
    TickType_t period = 0;
    bool autoReload = false;
    void* id = nullptr;
    TimerCallbackFunction_t callback = nullptr;
  };

  SimTask g_loopTask;

}  // namespace

extern "C" {

// ---------------------------------------------------------------- semaphores

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new SimMutex(); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return new SimMutex(); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  auto* m = new SimMutex();
  m->binary = true;
  return m;
}
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maxCount, UBaseType_t initialCount) {
  auto* m = new SimMutex();
  m->count = (int)initialCount;
  return m;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticksToWait) {
  auto* m = (SimMutex*)semaphore;
  if (!m) return pdFALSE;
  if (ticksToWait == portMAX_DELAY) {
    m->mutex.lock();
    return pdTRUE;
  }
  const bool locked = m->mutex.try_lock_for(std::chrono::milliseconds(ticksToWait));
  return locked ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  auto* m = (SimMutex*)semaphore;
  if (!m) return pdFALSE;
  m->mutex.unlock();
  return pdTRUE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t ticksToWait) {
  return xSemaphoreTake(semaphore, ticksToWait);
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) {
  return xSemaphoreGive(semaphore);
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* higherPriorityTaskWoken) {
  if (higherPriorityTaskWoken) *higherPriorityTaskWoken = pdFALSE;
  return xSemaphoreGive(semaphore);
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete (SimMutex*)semaphore; }

// ---------------------------------------------------------------- queues

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  auto* q = new SimQueue();
  q->itemSize = itemSize;
  q->capacity = length;
  return q;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticksToWait) {
  auto* q = (SimQueue*)queue;
  if (!q || !item) return pdFALSE;
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.size() >= q->capacity) return pdFALSE;
  const uint8_t* bytes = (const uint8_t*)item;
  q->items.emplace_back(bytes, bytes + q->itemSize);
  q->cv.notify_one();
  return pdTRUE;
}

BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t ticksToWait) {
  return xQueueSend(queue, item, ticksToWait);
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken) {
  if (woken) *woken = pdFALSE;
  return xQueueSend(queue, item, 0);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* buffer, TickType_t ticksToWait) {
  auto* q = (SimQueue*)queue;
  if (!q || !buffer) return pdFALSE;
  std::unique_lock<std::mutex> lock(q->mutex);
  if (q->items.empty()) {
    if (ticksToWait == 0) return pdFALSE;
    const auto timeout = ticksToWait == portMAX_DELAY ? std::chrono::milliseconds(1000)
                                                      : std::chrono::milliseconds(ticksToWait);
    q->cv.wait_for(lock, timeout, [q] { return !q->items.empty(); });
    if (q->items.empty()) return pdFALSE;
  }
  memcpy(buffer, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  auto* q = (SimQueue*)queue;
  if (!q) return 0;
  std::lock_guard<std::mutex> lock(q->mutex);
  return (UBaseType_t)q->items.size();
}

void vQueueDelete(QueueHandle_t queue) { delete (SimQueue*)queue; }

// ---------------------------------------------------------------- tasks

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* parameters,
                       UBaseType_t priority, TaskHandle_t* createdTask) {
  auto* task = new SimTask();
  task->name = name ? name : "task";
  task->thread = std::thread([fn, parameters] {
    if (fn) fn(parameters);
  });
  task->thread.detach();
  if (createdTask) *createdTask = task;
  return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackDepth,
                                   void* parameters, UBaseType_t priority,
                                   TaskHandle_t* createdTask, BaseType_t core) {
  return xTaskCreate(fn, name, stackDepth, parameters, priority, createdTask);
}

void vTaskDelete(TaskHandle_t task) {
  if (!task) return;  // NULL means "delete self"; the host thread just returns instead
  auto* t = (SimTask*)task;
  t->stop = true;
}

void vTaskDelay(TickType_t ticks) { sim::clock().advanceUs((uint64_t)ticks * 1000); }

void vTaskDelayUntil(TickType_t* previousWakeTime, TickType_t increment) {
  vTaskDelay(increment);
  if (previousWakeTime) *previousWakeTime = sim::clock().millis();
}

void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority) {
  (void)task;
  (void)priority;
}
UBaseType_t uxTaskPriorityGet(TaskHandle_t task) {
  (void)task;
  return 10;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &g_loopTask; }
TickType_t xTaskGetTickCount(void) { return sim::clock().millis(); }

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  (void)task;
  return 4096;  // host stacks are not measured; report a plausible headroom
}

const char* pcTaskGetName(TaskHandle_t task) {
  auto* t = (SimTask*)task;
  return t ? t->name.c_str() : "loop";
}

BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action) {
  auto* t = (SimTask*)task;
  if (!t) return pdFALSE;
  if (action == eIncrement) {
    t->notifyValue++;
  } else {
    t->notifyValue = value;
  }
  return pdTRUE;
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t value, eNotifyAction action,
                              BaseType_t* woken) {
  if (woken) *woken = pdFALSE;
  return xTaskNotify(task, value, action);
}

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticksToWait) {
  auto* t = &g_loopTask;
  const uint32_t value = t->notifyValue.load();
  if (clearOnExit) t->notifyValue = 0;
  if (value == 0 && ticksToWait > 0) vTaskDelay(ticksToWait > 10 ? 10 : ticksToWait);
  return value;
}

BaseType_t xTaskNotifyWait(uint32_t clearOnEntry, uint32_t clearOnExit, uint32_t* value,
                           TickType_t ticksToWait) {
  const uint32_t v = ulTaskNotifyTake(clearOnExit != 0, ticksToWait);
  if (value) *value = v;
  return v ? pdTRUE : pdFALSE;
}

// ---------------------------------------------------------------- software timers

TimerHandle_t xTimerCreate(const char* name, TickType_t period, BaseType_t autoReload,
                           void* timerId, TimerCallbackFunction_t callback) {
  auto* t = new SimTimer();
  t->name = name ? name : "timer";
  t->period = period;
  t->autoReload = autoReload != 0;
  t->id = timerId;
  t->callback = callback;
  return t;
}

BaseType_t xTimerStart(TimerHandle_t timer, TickType_t ticksToWait) {
  // Only the BLE task uses software timers, and that subsystem is not emulated; accepting the
  // start without scheduling anything keeps its setup path honest about succeeding.
  (void)timer;
  (void)ticksToWait;
  return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t timer, TickType_t ticksToWait) { return pdPASS; }

BaseType_t xTimerDelete(TimerHandle_t timer, TickType_t ticksToWait) {
  delete (SimTimer*)timer;
  return pdPASS;
}

void* pvTimerGetTimerID(TimerHandle_t timer) {
  auto* t = (SimTimer*)timer;
  return t ? t->id : nullptr;
}

// ---------------------------------------------------------------- heap

uint32_t xPortGetFreeHeapSize(void) { return 200 * 1024; }
uint32_t xPortGetMinimumEverFreeHeapSize(void) { return 150 * 1024; }

}  // extern "C"
