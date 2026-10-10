#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nurture.h"

// Absolute UTC anchor plus online time already earned since that anchor.
// Persist together with stamina: neither a reset nor a failed save can pay twice.
typedef struct { int64_t epoch_us, online_us; } rest_clock_t;
#define REST_CLOCK_MIN_US (1577836800LL * 1000000)
#define REST_CLOCK_MAX_US (4102444800LL * 1000000)
#define REST_CLOCK_HOUR_US 3600000000LL
static inline bool rest_clock_valid(const rest_clock_t *c) {
 return c && (c->epoch_us==0 || (c->epoch_us>=REST_CLOCK_MIN_US && c->epoch_us<=REST_CLOCK_MAX_US))
     && c->online_us>=0 && c->online_us<=REST_CLOCK_MAX_US;
}
static inline rest_clock_t rest_clock_snapshot(rest_clock_t base,int64_t elapsed_us) {
 if(base.epoch_us && elapsed_us>0){
  int64_t room=REST_CLOCK_MAX_US-base.online_us;
  base.online_us+=elapsed_us>room?room:elapsed_us;
 }
 return base;
}
// Caller applies the returned Q10 award to stamina, capped at NURT_MAX, and
// commits this candidate with the game state before publishing either change.
static inline bool rest_clock_sync(rest_clock_t *c,int64_t utc_us,int32_t *award) {
 if(!award || !rest_clock_valid(c) || utc_us<REST_CLOCK_MIN_US || utc_us>REST_CLOCK_MAX_US)return false;
 *award=0;
 if(c->epoch_us){
  int64_t accounted=c->epoch_us+c->online_us;
  int64_t offline=utc_us>accounted?utc_us-accounted:0;
  if(offline>REST_CLOCK_HOUR_US)offline=REST_CLOCK_HOUR_US;
  *award=(int32_t)(offline*NURT_STAMINA_RECOVER_PH/REST_CLOCK_HOUR_US);
  // Never rewind a paid anchor when an NTP server corrects the clock backwards.
  if(utc_us<accounted)utc_us=accounted;
  if(utc_us>REST_CLOCK_MAX_US)return false;
 }
 c->epoch_us=utc_us;c->online_us=0;return true;
}
