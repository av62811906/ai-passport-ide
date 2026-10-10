#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "encounter.h"
#include "items.h"
#define ACHIEVEMENT_COUNT 16
// Append-only IDs: claimed bits remain stable across content updates.
typedef struct { uint16_t evolutions, claimed; } achievement_store_t;
typedef struct { const char *name, *description; uint8_t kind; uint16_t target; uint8_t item, quantity; } achievement_info_t;
typedef struct { achievement_store_t store; uint16_t caught, shiny, defeated; } achievement_view_t;
// CAPPED is a successful claim: stock reaches its cap, excess is not added.
typedef enum { ACH_CLAIM_OK, ACH_CLAIM_LOCKED, ACH_CLAIM_ALREADY, ACH_CLAIM_CAPPED, ACH_CLAIM_FAILED } achievement_claim_t;
const achievement_info_t *achievement_info(unsigned id);
void achievement_view(achievement_view_t *out, const achievement_store_t *store, const dex_t *dex, uint16_t defeated);
unsigned achievement_progress(const achievement_view_t *view, unsigned id);
achievement_claim_t achievement_claim(achievement_store_t *store, inventory_t *inventory, const achievement_view_t *view, unsigned id);
