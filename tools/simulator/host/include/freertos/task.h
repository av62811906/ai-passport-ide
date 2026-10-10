// tools/simulator/host/include/freertos/task.h
// Host-side shim of <freertos/task.h>. xTaskCreate() really spawns a pthread;
// the returned handle stays valid for the task's whole life so applications can
// wait for completion or force-delete the task, mirroring FreeRTOS semantics.
#pragma once

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
