// tools/simulator/host/sim_audio.c
// Thread-safe ring buffer that stands in for the ES8311 microphone. The Python
// IDE captures the host microphone and pushes 16 kHz / 16-bit / mono PCM here;
// the application's capture task drains it through bsp_audio_read().
#include "sim.h"
#include "sim_internal.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

// Two seconds of audio at 16 kHz: enough headroom for scheduling jitter.
#define SIM_AUDIO_RING_SAMPLES (16000 * 2)

static int16_t s_ring[SIM_AUDIO_RING_SAMPLES];
static size_t s_ring_pos;   // index of the oldest sample
static size_t s_ring_len;   // number of valid samples

static pthread_mutex_t s_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_audio_cond = PTHREAD_COND_INITIALIZER;

static void make_deadline(struct timespec *ts, int timeout_ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

void sim_audio_push(const int16_t *samples, size_t count) {
    if (!samples || count == 0) return;

    pthread_mutex_lock(&s_audio_mutex);
    for (size_t i = 0; i < count; ++i) {
        if (s_ring_len == SIM_AUDIO_RING_SAMPLES) {
            // Overwrite the oldest sample so a stalled consumer cannot wedge us.
            s_ring_pos = (s_ring_pos + 1) % SIM_AUDIO_RING_SAMPLES;
            s_ring_len--;
        }
        s_ring[(s_ring_pos + s_ring_len) % SIM_AUDIO_RING_SAMPLES] = samples[i];
        s_ring_len++;
    }
    pthread_cond_signal(&s_audio_cond);
    pthread_mutex_unlock(&s_audio_mutex);
}

void sim_audio_read(int16_t *dst, size_t samples, int timeout_ms) {
    pthread_mutex_lock(&s_audio_mutex);

    struct timespec deadline;
    make_deadline(&deadline, timeout_ms);
    while (s_ring_len < samples) {
        if (pthread_cond_timedwait(&s_audio_cond, &s_audio_mutex, &deadline) == ETIMEDOUT) {
            break;
        }
    }

    size_t available = s_ring_len < samples ? s_ring_len : samples;
    for (size_t i = 0; i < available; ++i) {
        dst[i] = s_ring[s_ring_pos];
        s_ring_pos = (s_ring_pos + 1) % SIM_AUDIO_RING_SAMPLES;
    }
    s_ring_len -= available;
    if (available < samples) {
        memset(dst + available, 0, (samples - available) * sizeof(int16_t));
    }

    pthread_mutex_unlock(&s_audio_mutex);
}
