// FreeRTOS stand-in for the host emulator.
//
// The firmware uses a small slice of FreeRTOS: recursive mutexes around the message bus and SPI,
// a couple of queues and notifications, and task handles for heap accounting.  Each is mapped to
// the host equivalent.  Delays go through the virtual clock, so a vTaskDelay inside firmware code
// costs simulated time, not wall-clock time.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
typedef void* TimerHandle_t;
typedef void* EventGroupHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define errQUEUE_EMPTY 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFUL)
#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS 1
#define portTICK_RATE_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(ticks) ((uint32_t)(ticks))
#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 25
#define portYIELD_FROM_ISR(x) ((void)(x))
#define taskYIELD() vPortYield()
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define eSetValueWithOverwrite 3
#define eSetValueWithoutOverwrite 4
#define eIncrement 1
#define eNoAction 0

typedef int eNotifyAction;

// Critical sections have no meaning on the host's cooperative device thread, but the type and
// initialiser have to exist for code that declares a spinlock.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

void vPortYield(void);

// ---------------------------------------------------------------- semaphores
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maxCount, UBaseType_t initialCount);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t ticksToWait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* higherPriorityTaskWoken);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

// ---------------------------------------------------------------- queues
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticksToWait);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t ticksToWait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken);
BaseType_t xQueueReceive(QueueHandle_t queue, void* buffer, TickType_t ticksToWait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

// ---------------------------------------------------------------- tasks
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* parameters,
                       UBaseType_t priority, TaskHandle_t* createdTask);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackDepth,
                                   void* parameters, UBaseType_t priority,
                                   TaskHandle_t* createdTask, BaseType_t core);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t* previousWakeTime, TickType_t increment);
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority);
UBaseType_t uxTaskPriorityGet(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
const char* pcTaskGetName(TaskHandle_t task);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t value, eNotifyAction action,
                              BaseType_t* woken);
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticksToWait);
BaseType_t xTaskNotifyWait(uint32_t clearOnEntry, uint32_t clearOnExit, uint32_t* value,
                           TickType_t ticksToWait);

// ---------------------------------------------------------------- software timers
TimerHandle_t xTimerCreate(const char* name, TickType_t period, BaseType_t autoReload,
                           void* timerId, TimerCallbackFunction_t callback);
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t ticksToWait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t ticksToWait);
BaseType_t xTimerDelete(TimerHandle_t timer, TickType_t ticksToWait);
void* pvTimerGetTimerID(TimerHandle_t timer);

// ---------------------------------------------------------------- heap
uint32_t xPortGetFreeHeapSize(void);
uint32_t xPortGetMinimumEverFreeHeapSize(void);

#ifdef __cplusplus
}
#endif
