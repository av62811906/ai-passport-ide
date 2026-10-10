#include "audio_settings.h"
#include "sfx.h"
#include <stdatomic.h>
#include "nvs.h"

static atomic_bool s_muted = true;
static atomic_uchar s_volume = AUDIO_VOLUME_DEFAULT;

void audio_settings_init(void)
{
    uint8_t value = 1, volume = AUDIO_VOLUME_DEFAULT;
    nvs_handle_t handle;
    if (nvs_open("pokewalk", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, "mute", &value) != ESP_OK || value > 1) value = 1;
        if (nvs_get_u8(handle, "volume", &volume) != ESP_OK || volume > 100) volume = AUDIO_VOLUME_DEFAULT;
        nvs_close(handle);
    }
    atomic_store(&s_muted, value != 0);
    atomic_store(&s_volume, volume);
}

bool audio_settings_muted(void) { return atomic_load(&s_muted); }

bool audio_settings_set_muted(bool muted)
{
    if (muted == audio_settings_muted()) return true;
    nvs_handle_t handle;
    if (nvs_open("pokewalk", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, "mute", muted ? 1 : 0);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) return false;
    atomic_store(&s_muted, muted);
    sfx_notify_state();
    return true;
}

uint8_t audio_settings_volume(void) { return atomic_load(&s_volume); }
bool audio_settings_set_volume(uint8_t percent)
{
    if (percent > 100) return false;
    if (percent == audio_settings_volume()) return true;
    nvs_handle_t handle;
    if (nvs_open("pokewalk", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, "volume", percent);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) return false;
    atomic_store(&s_volume, percent);
    sfx_notify_state();
    return true;
}
