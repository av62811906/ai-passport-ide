#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "encounter.h"

#define ENC_REFRESH_HISTORY 64
#define ENC_AP_COOLDOWN_S 3600u
#define ENC_BASE_INTERVAL_S 14400u
#define ENC_HUNT_CREDIT_MAX 4096u
#define ENC_HUNT_CREDIT_STEP 1024u

typedef struct { uint32_t key, last_s; } enc_ap_history_t;
typedef struct {
    uint32_t online_s, next_base_s, hunt_q10, serial, discoveries;
    uint8_t history_count, history_next, since_rare, since_elite;
    uint8_t base_started, reserved[3];
    enc_ap_history_t history[ENC_REFRESH_HISTORY];
} enc_refresh_state_t;
typedef struct { uint8_t bssid[6]; int8_t rssi; uint8_t auth; bool has_ssid; } enc_refresh_ap_t;

static inline bool enc_refresh_valid(const enc_refresh_state_t *s)
{
    if (s->history_count > ENC_REFRESH_HISTORY || s->history_next >= ENC_REFRESH_HISTORY ||
        s->since_rare >= 8 || s->since_elite >= 30 || s->base_started > 1 ||
        s->hunt_q10 > ENC_HUNT_CREDIT_MAX) return false;
    for (unsigned i=0;i<s->history_count;i++) {
        if (!s->history[i].key || s->history[i].last_s > s->online_s) return false;
        for (unsigned j=0;j<i;j++) if(s->history[i].key==s->history[j].key)return false;
    }
    return true;
}
// Operates only on a candidate snapshot. Caller must persist queue, dex and
// refresh together before publishing or notifying. active_uid is reserved.
uint8_t enc_refresh_scan(enc_refresh_state_t *s, const enc_refresh_ap_t *aps,
    unsigned n, bool exploring, uint16_t distance_q10,
    enc_queue_t *queue, dex_t *dex, uint16_t active_uid);

uint8_t enc_refresh_collect(enc_refresh_state_t *s,const enc_refresh_ap_t *aps,unsigned n,
    bool exploring,uint16_t distance_q10,uint8_t room);

bool enc_refresh_observe(enc_refresh_state_t *,const enc_refresh_ap_t *,unsigned,bool,uint16_t);
