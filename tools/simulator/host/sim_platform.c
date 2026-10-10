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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
// The handle must stay meaningful for the task's whole life: applications poll
// it to know when a task has stopped, and vTaskDelete(handle) has to stop that
// task. A raw pthread_t cannot express both, so every task gets a small handle
// that records its thread plus a finished flag. Handles are intentionally never
// freed (one per created task, negligible for the simulator's lifetime).
// ---------------------------------------------------------------------------
typedef struct {
    TaskFunction_t entry;
    void *arg;
    pthread_t thread;
    volatile bool finished;   // set by the trampoline when the task exits
} sim_task_handle_t;

static void sim_task_mark_finished(void *data) {
    sim_task_handle_t *handle = (sim_task_handle_t *)data;
    handle->finished = true;
}

static void *sim_task_trampoline(void *data) {
    sim_task_handle_t *handle = (sim_task_handle_t *)data;
    // The cleanup handler also runs on pthread_exit(), so a task that deletes
    // itself (vTaskDelete(NULL)) still marks the handle as finished.
    pthread_cleanup_push(sim_task_mark_finished, handle);
    handle->entry(handle->arg);
    pthread_cleanup_pop(1);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *task) {
    (void)name; (void)stack; (void)priority;
    if (task) *task = NULL;

    sim_task_handle_t *handle = calloc(1, sizeof(*handle));
    if (!handle) return pdFAIL;
    handle->entry = entry;
    handle->arg = arg;

    // FreeRTOS makes the handle valid before the task ever runs; several apps
    // (the tuner included) rely on that to wait for a task's completion.
    if (task) *task = handle;

    if (pthread_create(&handle->thread, NULL, sim_task_trampoline, handle) != 0) {
        if (task) *task = NULL;
        free(handle);
        return pdFAIL;
    }
    pthread_detach(handle->thread);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    if (task == NULL) {
        pthread_exit(NULL);   // vTaskDelete(NULL): the calling task deletes itself
        return;
    }
    // Stopping another task: make it unwind, then wait briefly for it to be
    // gone. Deferred cancellation only lands at a cancellation point (the
    // audio read or the lock's polling sleep), so the task is never torn down
    // while it holds the LVGL lock.
    sim_task_handle_t *handle = (sim_task_handle_t *)task;
    if (handle->finished) return;
    pthread_cancel(handle->thread);
    for (int i = 0; i < 500 && !handle->finished; ++i) {
        usleep(1000);
    }
}

void vTaskDelay(TickType_t ticks) {
    usleep((useconds_t)ticks * 1000u);
}

// ---------------------------------------------------------------------------
// FreeRTOS: queues and mutexes. PokeWalk's sound task blocks on a queue and its
// world task guards shared state with a mutex, so both are real host primitives.
// ---------------------------------------------------------------------------
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;      // signalled whenever an item is added
    UBaseType_t capacity;
    UBaseType_t count;
    UBaseType_t item_size;
    UBaseType_t head;
    uint8_t *items;
} sim_queue_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    if (length == 0 || item_size == 0) return NULL;
    sim_queue_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->items = calloc(length, item_size);
    if (!q->items) {
        free(q);
        return NULL;
    }
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->ready, NULL);
    q->capacity = length;
    q->item_size = item_size;
    return q;
}

void vQueueDelete(QueueHandle_t queue) {
    sim_queue_t *q = (sim_queue_t *)queue;
    if (!q) return;
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->ready);
    free(q->items);
    free(q);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    sim_queue_t *q = (sim_queue_t *)queue;
    if (!q || !item) return pdFAIL;
    pthread_mutex_lock(&q->lock);
    if (q->count == q->capacity) {
        pthread_mutex_unlock(&q->lock);
        return pdFAIL;   // callers in the app always use a zero wait
    }
    const UBaseType_t slot = (q->head + q->count) % q->capacity;
    memcpy(q->items + (size_t)slot * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_signal(&q->ready);
    pthread_mutex_unlock(&q->lock);
    (void)wait;
    return pdTRUE;
}

static BaseType_t queue_take(QueueHandle_t queue, void *buffer, TickType_t wait,
                             bool remove) {
    sim_queue_t *q = (sim_queue_t *)queue;
    if (!q || !buffer) return pdFAIL;
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        if (wait == 0) {
            pthread_mutex_unlock(&q->lock);
            return pdFAIL;
        }
        pthread_cond_wait(&q->ready, &q->lock);
    }
    memcpy(buffer, q->items + (size_t)q->head * q->item_size, q->item_size);
    if (remove) {
        q->head = (q->head + 1) % q->capacity;
        q->count--;
    }
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t wait) {
    return queue_take(queue, buffer, wait, true);
}

BaseType_t xQueuePeek(QueueHandle_t queue, void *buffer, TickType_t wait) {
    return queue_take(queue, buffer, wait, false);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m) return NULL;
    pthread_mutex_init(m, NULL);
    return m;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    (void)wait;
    if (!semaphore) return pdFAIL;
    pthread_mutex_lock((pthread_mutex_t *)semaphore);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (!semaphore) return pdFAIL;
    pthread_mutex_unlock((pthread_mutex_t *)semaphore);
    return pdTRUE;
}

// ---------------------------------------------------------------------------
// BSP: audio output. The desktop has no speaker; these stubs keep the game's
// sound task running and silent rather than blocking or faulting.
// ---------------------------------------------------------------------------
esp_err_t bsp_audio_init(void) {
    return ESP_OK;
}

esp_err_t bsp_audio_write(const void *pcm, size_t bytes) {
    (void)pcm; (void)bytes;
    return ESP_OK;   // discard: no output device on the host
}

esp_err_t bsp_audio_suspend(void) {
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// BSP: panel sleep. The game's idle timer asks the panel to sleep; on the
// desktop the backlight stub already reflects brightness, so sleeping is a
// no-op that always succeeds.
// ---------------------------------------------------------------------------
esp_err_t bsp_display_sleep(bool sleep) {
    (void)sleep;
    return ESP_OK;
}
