// tools/simulator/host/sim_platform.c
// Host stubs for the ESP-IDF / BSP / FreeRTOS surface the application uses.
// Hardware absent on a desktop (battery) degrades exactly like the firmware
// does when that hardware fails. Microphone audio is real: it is captured by
// the Python IDE and delivered through the ring buffer in sim_audio.c.
#include "sim.h"
#include "sim_internal.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Log sink
// ---------------------------------------------------------------------------
static sim_log_cb_t s_log_cb;
static void *s_log_user;

void sim_set_log_callback(sim_log_cb_t cb, void *user) {
    s_log_cb = cb;
    s_log_user = user;
}

void sim_log_emit(sim_log_level_t level, const char *tag, const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    const char *letter = "D";
    switch (level) {
        case SIM_LOG_ERROR: letter = "E"; break;
        case SIM_LOG_WARN: letter = "W"; break;
        case SIM_LOG_INFO: letter = "I"; break;
        default: break;
    }

    char line[576];
    snprintf(line, sizeof(line), "[%s] %s: %s", letter, tag ? tag : "", message);

    if (s_log_cb) s_log_cb(line, s_log_user);
    fprintf(stderr, "%s\n", line);
}

// ---------------------------------------------------------------------------
// esp_err
// ---------------------------------------------------------------------------
const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        default: return "ESP_ERR_UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// BSP: display backlight
// ---------------------------------------------------------------------------
static int s_backlight = 100;

void bsp_display_backlight(uint8_t percent) {
    s_backlight = percent;
}

int sim_backlight_percent(void) {
    return s_backlight;
}

// ---------------------------------------------------------------------------
// BSP: LVGL lock. A real recursive mutex, because the capture task updates the
// UI on its own thread while the IDE main thread runs lv_timer_handler().
// ---------------------------------------------------------------------------
static pthread_mutex_t s_lvgl_mutex;
static pthread_once_t s_lvgl_mutex_once = PTHREAD_ONCE_INIT;

static void lvgl_mutex_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lvgl_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

bool bsp_lvgl_lock(int timeout_ms) {
    pthread_once(&s_lvgl_mutex_once, lvgl_mutex_init);
    if (timeout_ms < 0) {
        pthread_mutex_lock(&s_lvgl_mutex);
        return true;
    }
    // pthread_mutex_timedlock() is not available on macOS; poll trylock instead.
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        if (pthread_mutex_trylock(&s_lvgl_mutex) == 0) return true;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
                        + (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed_ms >= timeout_ms) return false;
        usleep(1000);  // 1 ms; contention is brief in practice
    }
}

void bsp_lvgl_unlock(void) {
    pthread_mutex_unlock(&s_lvgl_mutex);
}

// ---------------------------------------------------------------------------
// BSP: audio. Format/volume are accepted; reads are served from the ring
// buffer that the IDE fills from the host microphone.
// ---------------------------------------------------------------------------
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch) {
    (void)hz; (void)bits; (void)ch;
    return ESP_OK;
}

void bsp_audio_set_volume(uint8_t percent) {
    (void)percent;
}

esp_err_t bsp_audio_read(void *pcm, size_t bytes) {
    if (!pcm || bytes < sizeof(int16_t)) return ESP_FAIL;
    // Never fails: on a dry ring it returns silence, which the tuner treats as
    // "no signal" instead of tearing the capture task down.
    sim_audio_read((int16_t *)pcm, bytes / sizeof(int16_t), 200);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// BSP: battery is unavailable on the desktop.
// ---------------------------------------------------------------------------
int bsp_battery_soc(void) {
    return -1;
}

// ---------------------------------------------------------------------------
// FreeRTOS: map tasks onto detached pthreads so the capture task really runs.
// ---------------------------------------------------------------------------
typedef struct {
    TaskFunction_t entry;
    void *arg;
} sim_task_arg_t;

static void *sim_task_trampoline(void *data) {
    sim_task_arg_t *task = (sim_task_arg_t *)data;
    TaskFunction_t entry = task->entry;
    void *arg = task->arg;
    free(task);
    entry(arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *task) {
    (void)name; (void)stack; (void)priority;
    if (task) *task = NULL;

    sim_task_arg_t *data = malloc(sizeof(*data));
    if (!data) return pdFAIL;
    data->entry = entry;
    data->arg = arg;

    pthread_t thread;
    if (pthread_create(&thread, NULL, sim_task_trampoline, data) != 0) {
        free(data);
        return pdFAIL;
    }
    pthread_detach(thread);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
    pthread_exit(NULL);
}

void vTaskDelay(TickType_t ticks) {
    usleep((useconds_t)ticks * 1000u);
}
