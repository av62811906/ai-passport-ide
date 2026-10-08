// tools/simulator/host/include/freertos/task.h
// Host-side shim of <freertos/task.h>. xTaskCreate reports success but does not
// spawn a thread: the simulated app is started with audio unavailable, so the
// tuner's capture_task is never entered. The symbols exist only to link.
#pragma once

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
