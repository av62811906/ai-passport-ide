// tools/simulator/host/include/freertos/FreeRTOS.h
// Host-side shim of the FreeRTOS kernel for the desktop simulator.
// The simulated app runs single-threaded from the IDE main loop, so only the
// types/macros the compiled application references are provided.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configMAX_PRIORITIES 25
