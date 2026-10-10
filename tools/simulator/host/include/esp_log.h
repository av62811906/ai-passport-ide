// tools/simulator/host/include/esp_log.h
// Host-side shim of ESP-IDF <esp_log.h> for the desktop simulator.
// ESP_LOGx are redirected to sim_log_emit(), which forwards one formatted line
// to the IDE's registered callback (and mirrors it to stderr).
#pragma once

// ESP-IDF's esp_log.h pulls these in, and application code relies on that
// transitive availability (e.g. world.c uses printf/PRId64 without including
// them itself). Keep the same surface here.
#include <inttypes.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIM_LOG_ERROR = 0,
    SIM_LOG_WARN,
    SIM_LOG_INFO,
    SIM_LOG_DEBUG,
} sim_log_level_t;

void sim_log_emit(sim_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#ifdef __cplusplus
}
#endif

#define ESP_LOGE(tag, fmt, ...) sim_log_emit(SIM_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) sim_log_emit(SIM_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) sim_log_emit(SIM_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) sim_log_emit(SIM_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
