// main/save.c —— S18 存档。设计与取舍见 save.h。

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "save.h"
#include "exp.h"

static const char *TAG = "save";

// NVS 初始化。**存档必须自己负责这件事**，不能指望别人先做。
//
// 第一版没有这个函数 —— nvs_flash_init 藏在 world.c 的
// wifi_bring_up() 里，而 world_start 的顺序是「先读档、后起 WiFi」。
// 于是读档时 NVS 还没挂载，nvs_open 直接失败，
// **表现为「每次开机都是新游戏」而写入明明成功**。
//
// 教训是所有权：存档不依赖 WiFi，就不该等 WiFi 顺手把 NVS 带起来。
// 幂等 —— 重复调用返回 ESP_OK，所以 wifi_bring_up 那边不用改。
bool save_init(void)
{
    esp_err_t e = nvs_flash_init();
    // Initialization failure is not permission to erase a player's save.
    // Preserve the partition and surface the unavailable state to onboarding.
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s —— 存档不可用", esp_err_to_name(e));
        return false;
    }
    return true;
}

#define NS "pokewalk"      // NVS 命名空间
#define KEY "state"        // 整个 save_t 当一个 blob 存
#define KEY_OPENING "opening"

// 整块存而不是逐字段存 kv。
//
// 逐字段的好处是能单独更新（比如只改图鉴不动队列），
// 但那样**一次存档要 8 次 nvs_set + 8 次可能失败的点**，
// 而且字段加减时要同步维护键名表。
//
// 整块约 2.2 KiB，NVS 的 blob 上限是 508000 字节 —— 绰绰有余。
// 原子性也更好：要么整块新的，要么整块旧的，不会出现
// 「图鉴是新的而队列是旧的」这种半更新状态。

static bool policies_valid(const save_t *s) {
 for(unsigned i=0;i<PARTY_MAX+BOX_SPECIES;i++){
  if(!move_policy_valid(&s->move_policies[i]))return false;
  bool active=i<PARTY_MAX?i<s->party[0]:s->party[2+i*MON_BYTES]!=0;
  if(!active)for(unsigned b=0;b<MOVE_POLICY_BYTES;b++)if(s->move_policies[i].disabled[b])return false;
 }
 return true;
}
bool save_write(const save_t *s)
{
    if (!s || s->version != SAVE_VERSION || !policies_valid(s) || !items_inventory_valid(&s->inventory) || !trainer_store_valid(&s->challenge) || !enc_refresh_valid(&s->refresh) || !exploration_valid(&s->exploration) || !exploration_regions_valid(&s->regions) || !exploration_updates_valid(&s->exploration_updates) || !rest_clock_valid(&s->rest_clock) || !dungeon_progress_valid(&s->dungeon)) return false;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "打开失败: %s", esp_err_to_name(e));
        return false;
    }

    e = nvs_set_blob(h, KEY, s, sizeof(*s));
    // opening_seen 是单调标记。world 的快照不拥有它，传 false 时绝不
    // 擦掉已经写入的 true；显式传 true 的调用方则一并持久化。
    if (e == ESP_OK && s->opening_seen) {
        e = nvs_set_u8(h, KEY_OPENING, 1);
    }
    if (e == ESP_OK) {
        // **commit 不能省** —— nvs_set_blob 只写进缓存，
        // 不 commit 的话拔电就丢了，而函数返回值是成功的。
        e = nvs_commit(h);
    }
    nvs_close(h);

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "写入失败: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

save_read_result_t save_read_status(save_t *out)
{
    if (!out) return SAVE_READ_ERROR;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return SAVE_READ_EMPTY;
    if (e != ESP_OK) return SAVE_READ_ERROR;

    size_t len = 0;
    e = nvs_get_blob(h, KEY, NULL, &len);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return SAVE_READ_EMPTY;
    }
    if (e != ESP_OK || len > sizeof(*out) || len < sizeof(uint16_t)) {
        nvs_close(h);
        return SAVE_READ_ERROR;
    }
    size_t expected_len = len;
    memset(out, 0, sizeof(*out));
    e = nvs_get_blob(h, KEY, out, &len);
    uint8_t opening_seen = 0;
    if (e == ESP_OK) (void)nvs_get_u8(h, KEY_OPENING, &opening_seen);
    nvs_close(h);
    if (e != ESP_OK || len != expected_len) return SAVE_READ_ERROR;
    return save_decode(out, out, len, opening_seen);
}

save_read_result_t save_decode(save_t *out, const void *blob, size_t len, uint8_t opening_seen)
{
    if (!out || !blob || len < sizeof(uint16_t) || len > sizeof(*out)) return SAVE_READ_ERROR;
    bool legacy = len == sizeof(save_v5_t);
    bool version6 = len == sizeof(save_v6_t);
    bool version7 = len == sizeof(save_v7_t);
    bool version8 = len == sizeof(save_v8_t);
    bool version9 = len == sizeof(save_v9_t);
    bool version10 = len == sizeof(save_v10_t);
    bool version15 = len == sizeof(save_v15_t);
    bool version16 = len == sizeof(save_v16_t);
    bool version17 = len == sizeof(save_v17_t);
    bool version18 = len == sizeof(save_v18_t);
    bool version19 = len == sizeof(save_v19_t);
    bool version20 = len == sizeof(save_v20_t);
    if (!legacy && !version6 && !version7 && !version8 && !version9 && !version10 &&
        !version15 && !version16 && !version17 && !version18 && !version19 && !version20 && len != sizeof(save_v14_t) && len != sizeof(*out))
        return SAVE_READ_ERROR;
    memmove(out, blob, len);
    memset((uint8_t *)out + len, 0, sizeof(*out) - len);
    bool version14 = len == sizeof(save_v14_t) && out->version == 14;
    bool version13 = len == sizeof(save_v14_t) && out->version == 13;
    bool version12 = len == sizeof(save_v14_t) && out->version == 12;
    bool version11 = len == sizeof(save_v14_t) && out->version == 11;
    if(len==sizeof(save_v14_t)&&!(version11||version12||version13||version14))return SAVE_READ_ERROR;
    if (out->version != (legacy ? SAVE_LEGACY_VERSION : version6 ? 6 : version7 ? 7 : version8 ? 8 : version9 ? 9 : version10 ? 10 : version11 ? 11 : version12 ? 12 : version13 ? 13 : version14 ? 14 : version15 ? 15 : version16 ? 16 : version17 ? 17 : version18 ? 18 : version19 ? 19 : version20 ? 20 : SAVE_VERSION)) {
        ESP_LOGW(TAG, "存档版本 %u ≠ %d —— 保留原档，禁止新游戏覆盖",
                 out->version, SAVE_VERSION);
        return SAVE_READ_ERROR;
    }
    if(out->version<21)memset(out->move_policies,0,sizeof(out->move_policies));
    if(!policies_valid(out))return SAVE_READ_ERROR;
    if(out->version<20)out->playtime_s=0;
    bool pre19=out->version<19;
    bool pre18=out->version<18;
    bool pre16=out->version<16;
    if(pre16)for(unsigned i=0;i<ENC_QUEUE_CAP;i++){out->queue.items[i].level=0;out->queue.items[i].activity=0;}
    if(!pre16)for(unsigned i=0;i<ENC_QUEUE_CAP;i++)if(out->queue.items[i].level>100||out->queue.items[i].activity>(pre19?8:ENC_ACTIVITY_EXPLORATION))return SAVE_READ_ERROR;
    if(pre19)out->exploration_wins=0;
    if(version15||version16||version17||version18||version19||version20)out->version=SAVE_VERSION;
    // V5-V11 used species-indexed cells. Validate before adopting physical slots.
    if (out->version < 12) {
        for(unsigned i=0;i<BOX_SPECIES;i++) {
            uint8_t id=out->party[2+(PARTY_MAX+i)*MON_BYTES];
            if(id && id!=i+1)return SAVE_READ_ERROR;
        }
    }
    if(version11||version12||version13||version14)out->version=SAVE_VERSION;
    // A raw blob can contain an invalid _Bool representation. Compare its bytes
    // before evaluating it so malformed data is rejected without undefined reads.
    const bool no = false, yes = true;
    if (memcmp(&out->opening_seen, &no, sizeof(no)) != 0 &&
        memcmp(&out->opening_seen, &yes, sizeof(yes)) != 0) return SAVE_READ_ERROR;
    out->opening_seen = out->opening_seen || opening_seen != 0;
    if (legacy) {
        items_inventory_init(&out->inventory);
        out->version = SAVE_VERSION;
    } else if (!items_inventory_valid(&out->inventory)) return SAVE_READ_ERROR;
    if (legacy || version6) {
        out->version = SAVE_VERSION;
        // Older saves have no victory counter: grandfather existing adventurers.
        out->challenge.wild_wins = out->species != 0;
    }
    if (version7 || version8) {
        // Read old tail before overwriting it: expanded mons have different strides.
        trainer_store_v8_t previous;
        memcpy(&previous,(uint8_t *)out+sizeof(save_v6_t),sizeof(previous));
        achievement_store_t achievements={0};
        if(version8)memcpy(&achievements,(uint8_t *)out+offsetof(save_v8_t,achievements),sizeof(achievements));
        trainer_store_t *next=&out->challenge;memset(next,0,sizeof(*next));
        next->wild_wins=previous.wild_wins;next->defeated=previous.defeated;
        next->league_stage=previous.league_stage;next->league_active=previous.league_active;
        // Session header has identical fields; only side/mon strides changed.
        memcpy(&next->session.rng,&previous.session.rng,sizeof(previous.session)-offsetof(trainer_session_v8_t,rng));
        next->session.pending_move=0;
        for(unsigned side=0;side<2;side++){
            trainer_side_t *d=&next->session.sides[side];const trainer_side_v8_t *s=&previous.session.sides[side];
            d->count=s->count;d->active=s->active;d->reflect=s->reflect;d->light_screen=s->light_screen;
            for(unsigned i=0;i<6;i++)memcpy(&d->mons[i],&s->mons[i],sizeof(s->mons[i]));
        }
        out->achievements=achievements;out->version=SAVE_VERSION;
    }
    if (legacy || version6 || version7 || version8 || version9) {
        memset(&out->refresh,0,sizeof(out->refresh));
        uint64_t seconds=out->last_uptime_us>0?(uint64_t)out->last_uptime_us/1000000:0;
        out->refresh.online_s=seconds>UINT32_MAX-ENC_BASE_INTERVAL_S?UINT32_MAX-ENC_BASE_INTERVAL_S:(uint32_t)seconds;
        // Existing players do not receive another boot gift on migration.
        out->refresh.base_started=out->scans!=0 || out->species!=0;
        out->refresh.next_base_s=out->refresh.online_s+ENC_BASE_INTERVAL_S;
        out->version=SAVE_VERSION;
    }
    if (legacy || version6 || version7 || version8 || version9 || version10) {
        exploration_init(&out->exploration);
        out->version=SAVE_VERSION;
    }
    // Early V11 called this byte reserved[0], with no range constraint. Later
    // V11 builds introduced tracking without a schema bump. Preserve valid
    // tracked species from those builds; an impossible ID means no old target.
    // V12+ owned and validated the field, so malformed modern IDs still fail.
    if (version11 && out->exploration.tracked_species > DEX_SPECIES)
        out->exploration.tracked_species = 0;
    // V15/V16 receipt tail bytes were C padding, not partner reward fields.
    // Never interpret an old padding value as a new reward (or corrupt save).
    if (version15 || version16) {
        out->dungeon.receipt.partner_species = 0;
        out->dungeon.receipt.partner_shiny = 0;
    }
    if (!exploration_valid(&out->exploration)) return SAVE_READ_ERROR;
    if(pre18){memset(&out->regions,0,sizeof(out->regions));out->regions.selected=out->exploration.route;}
    if(version18)for(unsigned i=0;i<EXPLORATION_REGIONS;i++)if(out->regions.region[i].pity>5)return SAVE_READ_ERROR;
    if(!exploration_regions_valid(&out->regions))return SAVE_READ_ERROR;
    if (!enc_refresh_valid(&out->refresh)) return SAVE_READ_ERROR;
    if (!trainer_store_valid(&out->challenge)) return SAVE_READ_ERROR;
    bool migrated=legacy||version6||version7||version8||version9||version10||version11||version12;
    if(migrated){
        for(unsigned side=0;side<2;side++){
            trainer_side_t *t=&out->challenge.session.sides[side];
            if(t->count>TRAINER_SLOTS)return SAVE_READ_ERROR;
            for(unsigned i=0;i<t->count;i++){
                if(!combat_valid(&t->mons[i]))return SAVE_READ_ERROR;
                combat_migrate_gen2(&t->mons[i]);
            }
        }
    }
    if(migrated||version13)out->exploration.research_flags=0;
    if(!rest_clock_valid(&out->rest_clock))return SAVE_READ_ERROR;
    if(!exploration_updates_valid(&out->exploration_updates))return SAVE_READ_ERROR;
    if(!dungeon_progress_valid(&out->dungeon))return SAVE_READ_ERROR;
    return migrated||version13||version14||version15||version16||version17||version18||version19||version20 ? SAVE_READ_MIGRATED : SAVE_READ_OK;
}

bool save_read(save_t *out) {
    save_read_result_t result = save_read_status(out);
    return result == SAVE_READ_OK || result == SAVE_READ_MIGRATED;
}

bool save_opening_seen(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t seen = 0;
    esp_err_t e = nvs_get_u8(h, KEY_OPENING, &seen);
    nvs_close(h);
    return e == ESP_OK && seen != 0;
}

bool save_mark_opening_seen(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return false;
    e = nvs_set_u8(h, KEY_OPENING, 1);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "开场标记写入失败: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

bool save_exists(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t e = nvs_get_blob(h, KEY, NULL, &len);
    nvs_close(h);
    return e == ESP_OK && (len == sizeof(save_t) || len == sizeof(save_v20_t) || len == sizeof(save_v19_t) || len == sizeof(save_v18_t) || len == sizeof(save_v17_t) || len == sizeof(save_v16_t) || len == sizeof(save_v15_t) || len == sizeof(save_v14_t) || len == sizeof(save_v10_t) || len == sizeof(save_v9_t) || len == sizeof(save_v8_t) || len == sizeof(save_v7_t) || len == sizeof(save_v6_t) || len == sizeof(save_v5_t));
}

bool save_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t state_e = nvs_erase_key(h, KEY);
    esp_err_t opening_e = nvs_erase_key(h, KEY_OPENING);
    bool state_ok = state_e == ESP_OK || state_e == ESP_ERR_NVS_NOT_FOUND;
    bool opening_ok = opening_e == ESP_OK || opening_e == ESP_ERR_NVS_NOT_FOUND;
    esp_err_t e = (state_ok && opening_ok) ? nvs_commit(h) : ESP_FAIL;
    nvs_close(h);
    return e == ESP_OK;
}

static bool saved_bool_valid(const bool *value)
{
    const bool no = false, yes = true;
    // Read the representation as bytes: evaluating a malformed persisted _Bool
    // before validation would itself be undefined behavior.
    return memcmp(value, &no, sizeof(no)) == 0 ||
           memcmp(value, &yes, sizeof(yes)) == 0;
}

static bool loaded_queue_valid(const enc_queue_t *queue)
{
    if (queue->count > ENC_QUEUE_CAP) return false;
    for (unsigned i = 0; i < queue->count; i++) {
        const encounter_t *entry = &queue->items[i];
        if (!entry->uid || entry->species_id < 1 || entry->species_id > DEX_SPECIES ||
            entry->rarity < 1 || entry->rarity > 5 || entry->hp_ratio > 100 ||
            !saved_bool_valid(&entry->is_shiny) ||
            !saved_bool_valid(&entry->is_transient) ||
            !saved_bool_valid(&entry->exp_granted)) return false;
        for (unsigned prior = 0; prior < i; prior++)
            if (queue->items[prior].uid == entry->uid) return false;
    }
    // next_uid == 0 is the valid state immediately after assigning UINT16_MAX.
    // Removed entries leave stale tail bytes; only the active prefix is owned.
    return true;
}

static bool loaded_party_valid(const save_t *saved, party_t *party)
{
    const uint8_t *raw = saved->party;
    if (raw[0] > PARTY_MAX) return false;
    uint8_t box_count = 0;
    for (unsigned i = 0; i < PARTY_MAX + BOX_SPECIES; i++) {
        const uint8_t *mon = raw + 2 + i*MON_BYTES;
        bool active = i < PARTY_MAX ? i < raw[0] : mon[0] != 0;
        if (!active) {
            for (unsigned b = 0; b < MON_BYTES; b++) if (mon[b] != 0) return false;
        } else {
            if (mon[0] < 1 || mon[0] > BOX_SPECIES || mon[1] < 1 || mon[1] > LEVEL_MAX)
                return false;
            if (i >= PARTY_MAX) {
                box_count++;
            }
        }
    }
    if (raw[1] != box_count ||
        !party_deserialize(party, raw, sizeof(saved->party))) return false;
    memcpy(party->policies,saved->move_policies,sizeof(party->policies));
    if (!party->party_count) {
        // Empty V5 snapshots are produced while a fresh game awaits a choice.
        // An absent leader with prior ownership/progress is not a new game.
        for (unsigned i = 0; i < DEX_BYTES; i++)
            if (saved->dex.caught[i] || saved->dex.shiny_caught[i]) return false;
        return party_total(party) == 0 && saved->species == 0 &&
               saved->level == 0 && saved->exp == 0;
    }
    return true;
}

bool save_validate_world(const save_t *saved, party_t *party)
{
    return saved && party && policies_valid(saved) && loaded_queue_valid(&saved->queue) && loaded_party_valid(saved, party);
}
