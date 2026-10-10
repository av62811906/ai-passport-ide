// tools/simulator/host/include/freertos/queue.h
// Host-side shim of the FreeRTOS queue API. PokeWalk's sound mixer task blocks
// on a queue, so the host implementation is a real blocking queue (mutex +
// condition variable), not a counter.
#pragma once

#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void *buffer, TickType_t wait);
