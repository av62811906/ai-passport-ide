// tools/simulator/host/sim_esp_idf.c
// Host-side ESP-IDF stubs added for the PokeWalk game: the monotonic clock, an
// in-memory NVS, the ROM CRC, and the Wi-Fi bring-up surface the world task
// calls unconditionally (the rest of its Wi-Fi paths are compiled out by
// HOST_BUILD). The desktop has no radio, so scanning yields nothing and the
// game's sensing layer simply reports no movement.
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>

// ---------------------------------------------------------------------------
// esp_timer: monotonic microseconds since an arbitrary epoch.
// ---------------------------------------------------------------------------
int64_t esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

// ---------------------------------------------------------------------------
// ROM CRC-32 (matches the device's esp_rom_crc32_le and PC-side zlib.crc32).
// ---------------------------------------------------------------------------
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len) {
    return (uint32_t)crc32((uLong)crc, (const Bytef *)buf, (uInt)len);
}

// ---------------------------------------------------------------------------
// NVS: an in-memory key/value store. Handles are 1-based namespace ids; entries
// are freed only at process exit, which is fine for the simulator's lifetime.
// A mutex mirrors the device store's thread safety: the game's world task writes
// saves from its own thread while the UI thread reads/writes settings.
// ---------------------------------------------------------------------------
#define SIM_NVS_MAX_NAMESPACES 8
#define SIM_NVS_MAX_ENTRIES 64

typedef struct {
    char name[16];
} sim_nvs_namespace_t;

typedef struct {
    int ns;               // 1-based namespace id
    char key[24];
    uint8_t *data;
    size_t len;
} sim_nvs_entry_t;

static sim_nvs_namespace_t s_ns[SIM_NVS_MAX_NAMESPACES];
static int s_ns_count;
static sim_nvs_entry_t s_entries[SIM_NVS_MAX_ENTRIES];
static int s_entry_count;
static pthread_mutex_t s_nvs_lock = PTHREAD_MUTEX_INITIALIZER;

esp_err_t nvs_flash_init(void) {
    return ESP_OK;   // the in-memory store needs no mounting
}

static esp_err_t nvs_open_locked(const char *name, nvs_open_mode_t mode,
                                 nvs_handle_t *out) {
    (void)mode;
    if (!name || !out || strlen(name) >= sizeof(s_ns[0].name)) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < s_ns_count; ++i) {
        if (strcmp(s_ns[i].name, name) == 0) {
            *out = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    if (s_ns_count >= SIM_NVS_MAX_NAMESPACES) return ESP_ERR_NO_MEM;
    strcpy(s_ns[s_ns_count].name, name);
    *out = (nvs_handle_t)(++s_ns_count);
    return ESP_OK;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) {
    pthread_mutex_lock(&s_nvs_lock);
    const esp_err_t result = nvs_open_locked(name, mode, out);
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

void nvs_close(nvs_handle_t handle) {
    (void)handle;
}

static sim_nvs_entry_t *nvs_find(nvs_handle_t handle, const char *key) {
    if (!key || strlen(key) >= sizeof(s_entries[0].key)) return NULL;
    for (int i = 0; i < s_entry_count; ++i) {
        if (s_entries[i].ns == (int)handle && strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static esp_err_t nvs_store(nvs_handle_t handle, const char *key,
                           const void *data, size_t len) {
    if (!key || strlen(key) >= sizeof(s_entries[0].key)) return ESP_ERR_INVALID_ARG;
    sim_nvs_entry_t *entry = nvs_find(handle, key);
    if (!entry) {
        if (s_entry_count >= SIM_NVS_MAX_ENTRIES) return ESP_ERR_NO_MEM;
        entry = &s_entries[s_entry_count++];
        memset(entry, 0, sizeof(*entry));
        entry->ns = (int)handle;
        strcpy(entry->key, key);
    }
    uint8_t *copy = malloc(len ? len : 1);
    if (!copy) return ESP_ERR_NO_MEM;
    if (len) memcpy(copy, data, len);
    free(entry->data);
    entry->data = copy;
    entry->len = len;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    pthread_mutex_lock(&s_nvs_lock);
    const esp_err_t result = nvs_store(handle, key, &value, sizeof(value));
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_nvs_lock);
    const sim_nvs_entry_t *entry = nvs_find(handle, key);
    esp_err_t result = ESP_OK;
    if (!entry || entry->len != sizeof(*out)) {
        result = ESP_ERR_NVS_NOT_FOUND;
    } else {
        *out = entry->data[0];
    }
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t len) {
    if (!data && len) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_nvs_lock);
    const esp_err_t result = nvs_store(handle, key, data, len);
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out,
                       size_t *len) {
    if (!len) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_nvs_lock);
    const sim_nvs_entry_t *entry = nvs_find(handle, key);
    esp_err_t result = ESP_OK;
    if (!entry) {
        result = ESP_ERR_NVS_NOT_FOUND;
    } else if (!out) {               // length query only
        *len = entry->len;
    } else if (*len < entry->len) {
        result = ESP_ERR_INVALID_SIZE;
    } else {
        memcpy(out, entry->data, entry->len);
        *len = entry->len;
    }
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    pthread_mutex_lock(&s_nvs_lock);
    sim_nvs_entry_t *entry = nvs_find(handle, key);
    esp_err_t result = ESP_OK;
    if (!entry) {
        result = ESP_ERR_NVS_NOT_FOUND;
    } else {
        const int index = (int)(entry - s_entries);
        free(s_entries[index].data);
        for (int i = index; i < s_entry_count - 1; ++i) s_entries[i] = s_entries[i + 1];
        s_entry_count--;
    }
    pthread_mutex_unlock(&s_nvs_lock);
    return result;
}

// ---------------------------------------------------------------------------
// Wi-Fi / netif / event. Bring-up succeeds so the game's boot path is unchanged;
// scanning finds nothing, which the sensing layer already handles.
// ---------------------------------------------------------------------------
esp_err_t esp_netif_init(void) {
    return ESP_OK;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void) {
    static char dummy;
    return (esp_netif_t *)&dummy;
}

esp_netif_t *esp_netif_create_default_wifi_ap(void) {
    static char dummy;
    return (esp_netif_t *)&dummy;
}

esp_err_t esp_event_loop_create_default(void) {
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    (void)mode;
    return ESP_OK;
}

esp_err_t esp_wifi_start(void) {
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) {
    (void)config; (void)block;
    return ESP_FAIL;   // no radio: the world task treats this as "no data"
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,
                                       wifi_ap_record_t *ap_records) {
    (void)ap_records;
    if (number) *number = 0;
    return ESP_OK;
}
