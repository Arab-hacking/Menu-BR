/*
 * probe.cpp — структурные пробы
 */
#include "br_common.h"

/* ---------- валидация ---------- */

bool br_validate_pool(uintptr_t pool_glob, int *valid_slots, uintptr_t *table_out)
{
    uint64_t tbl = 0;
    if (!br_safe_read(pool_glob, &tbl, 8)) return false;
    if (tbl == 0) return false;

    int valid = 0;
    int null_slots = 0;
    for (int i = 0; i < BR_MAX_VEHICLES; i++) {
        uint64_t w = 0;
        if (!br_safe_read(tbl + (uintptr_t)i * 8, &w, 8)) return false;
        if (w == 0) { null_slots++; continue; }
        uint16_t netid = 0;
        uint32_t cls = 0xFFFFFFFF;
        if (!br_safe_read(w, &netid, 2)) continue;
        if (netid >= BR_MAX_VEHICLES) continue;
        br_safe_read(w + 0x430, &cls, 4);
        if (cls <= 12) valid++;
    }
    if (valid_slots) *valid_slots = valid;
    if (table_out) *table_out = tbl;
    return valid >= 2; /* пустой мир (0 машин) считается невалидным — см. README */
}

bool br_pool_plausible(uintptr_t pool_glob, uintptr_t *table_out)
{
    uint64_t tbl = 0;
    if (!br_safe_read(pool_glob, &tbl, 8)) return false;
    if (tbl == 0) return false;

    static const int sample[] = { 0, 1, 2, 128, 512, 1024, 1536, 1999 };
    for (int s = 0; s < (int)(int)(sizeof(sample) / sizeof(sample[0])); s++) {
        uint64_t w = 0;
        if (!br_safe_read(tbl + (uintptr_t)sample[s] * 8, &w, 8)) return false;
        if (w == 0) continue;
        uint16_t netid = 0;
        if (!br_safe_read(w, &netid, 2)) return false;
        if (netid >= BR_MAX_VEHICLES) return false;
    }
    if (table_out) *table_out = tbl;
    return true;
}

bool br_validate_manager(uintptr_t mgr_glob, int *valid_ids, int *online_out)
{
    static const int sample[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 384, 512, 768, 1000, 1200, 1400, 1500, 1503 };
    int valid = 0, online = 0;
    for (int s = 0; s < (int)(int)(sizeof(sample) / sizeof(sample[0])); s++) {
        int id = sample[s];
        uint64_t obj = 0;
        if (!br_safe_read(mgr_glob + 8 + (uintptr_t)id * 8, &obj, 8)) return false;
        if (obj == 0) continue;
        uint32_t st = 0xFFFFFFFF;
        if (!br_safe_read(obj + 0x118, &st, 4)) continue;
        if (st > 0x40) continue;
        valid++;
        online++;
    }
    if (valid_ids) *valid_ids = valid;
    if (online_out) *online_out = online;
    return valid >= 2;
}

bool br_validate_ped_table(uintptr_t ped_glob, int *valid_out)
{
    static const int sample[] = { 0, 1, 2, 4, 16, 64, 256, 512, 1024, 1500 };
    int valid = 0;
    for (int s = 0; s < (int)(int)(sizeof(sample) / sizeof(sample[0])); s++) {
        int id = sample[s];
        float p[3];
        if (!br_readf(ped_glob + (uintptr_t)id * BR_PED_STRIDE + 0x38, p, 3)) return false;
        bool ok = true;
        for (int k = 0; k < 3; k++)
            if (!isfinite(p[k]) || fabsf(p[k]) > 1e6f) ok = false;
        if (ok) valid++;
    }
    if (valid_out) *valid_out = valid;
    return valid >= 3;
}

/* ---------- поиск ---------- */

/* Быстрая предпроверка кандидата (небольшая выборка слотов). */
static int pool_quick(uintptr_t tbl)
{
    static const int idx[] = { 0, 512, 1024, 1536, 1999 };
    int ok = 0;
    for (int k = 0; k < 5; k++) {
        uint64_t w = 0;
        if (!br_safe_read(tbl + (uintptr_t)idx[k] * 8, &w, 8)) return 0;
        if (w == 0) continue;
        uint16_t netid = 0;
        if (br_safe_read(w, &netid, 2) && netid < BR_MAX_VEHICLES) ok++;
    }
    return ok;
}

static int mgr_quick(uintptr_t tbl)
{
    static const int idx[] = { 0, 64, 256, 512, 1000, 1503 };
    int ok = 0;
    for (int k = 0; k < 6; k++) {
        uint64_t obj = 0;
        if (!br_safe_read(tbl + 8 + (uintptr_t)idx[k] * 8, &obj, 8)) return 0;
        if (obj == 0) continue;
        uint32_t st = 0;
        if (br_safe_read(obj + 0x118, &st, 4) && st <= 0x40) ok++;
    }
    return ok;
}

void br_probe_all(uintptr_t base, br_probe_t *res)
{
    memset(res, 0, sizeof(*res));

    br_span_t spans[16];
    int nspans = br_module_data_spans(base, spans, 16);
    if (nspans <= 0) return;

    /* 1) пул машин */
    uintptr_t best_pool = 0;
    int best_pool_score = 0;
    for (int s = 0; s < nspans; s++) {
        for (uintptr_t va = spans[s].start; va + 8 <= spans[s].end; va += 8) {
            uint64_t p = 0;
            if (!br_safe_read(va, &p, 8)) continue;
            if (p == 0) continue;
            if (p < 0x10000) continue;
            int q = pool_quick(p);
            if (q < 2) continue;
            int valid = 0;
            if (br_validate_pool(va, &valid, 0) && valid > best_pool_score) {
                best_pool_score = valid;
                best_pool = va;
            }
        }
    }
    if (best_pool) {
        res->pool_found = true;
        res->pool_ptr = best_pool;
        res->pool_valid_slots = best_pool_score;
        br_validate_pool(best_pool, 0, &res->pool_table);
    }

    /* 2) менеджер игроков: сначала по соседству с пулом (в обеих сборках
     *    менеджер лежит за 8 байт до пула), затем полный скан. */
    uintptr_t best_mgr = 0;
    int best_mgr_score = 0;
    if (best_pool) {
        for (int d = -32; d <= 32; d += 8) {
            uintptr_t cand = best_pool + (uintptr_t)(int)d;
            if (cand < best_pool - 32) continue;
            int valid = 0, online = 0;
            if (br_validate_manager(cand, &valid, &online) && valid > best_mgr_score) {
                best_mgr_score = valid;
                best_mgr = cand;
                res->mgr_found = true;
                res->mgr_ptr = cand;
                res->mgr_valid_ids = valid;
                res->players_online = online;
            }
        }
    }
    if (!res->mgr_found) {
        for (int s = 0; s < nspans; s++) {
            for (uintptr_t va = spans[s].start; va + 8 <= spans[s].end; va += 8) {
                uint64_t p = 0;
                if (!br_safe_read(va, &p, 8)) continue;
                if (p == 0 || p < 0x10000) continue;
                if (mgr_quick(p) < 2) continue;
                int valid = 0, online = 0;
                if (br_validate_manager(va, &valid, &online) && valid > best_mgr_score) {
                    best_mgr_score = valid;
                    best_mgr = va;
                    res->mgr_found = true;
                    res->mgr_ptr = va;
                    res->mgr_valid_ids = valid;
                    res->players_online = online;
                }
            }
        }
    }

    /* 3) таблица педов: полный скан (слабый критерий — только координаты) */
    uintptr_t best_ped = 0;
    int best_ped_score = 0;
    for (int s = 0; s < nspans; s++) {
        for (uintptr_t va = spans[s].start; va + 8 <= spans[s].end; va += 8) {
            uint64_t p = 0;
            if (!br_safe_read(va, &p, 8)) continue;
            if (p == 0 || p < 0x10000) continue;
            int valid = 0;
            if (br_validate_ped_table(p, &valid) && valid > best_ped_score) {
                best_ped_score = valid;
                best_ped = va;
            }
        }
    }
    if (best_ped) {
        res->ped_found = true;
        res->ped_ptr = best_ped;
        res->ped_valid_ids = best_ped_score;
    }
}

/* ---------- смещения внутри объекта игрока ---------- */

int br_find_player_veh_off(uintptr_t player_obj)
{
    /* обратная связь: CVehicle+0xB8 = RemotePlayer* */
    uint64_t self = player_obj;
    for (int off = 0; off <= 0x800; off += 8) {
        uint64_t q = 0;
        if (!br_safe_read(player_obj + off, &q, 8)) return 0;
        if (q == 0 || q < 0x10000) continue;
        uint64_t back = 0;
        if (br_safe_read(q + 0xB8, &back, 8) && back == self)
            return off;
    }
    return 0;
}

int br_find_player_ped_off(uintptr_t player_obj, uint16_t veh_id)
{
    int best = 0;
    for (int off = 0; off <= 0x800; off += 8) {
        uint64_t q = 0;
        if (!br_safe_read(player_obj + off, &q, 8)) return 0;
        if (q == 0 || q < 0x10000) continue;
        float p[3];
        if (!br_readf(q + 0x38, p, 3)) continue;
        bool finite = true;
        for (int k = 0; k < 3; k++)
            if (!isfinite(p[k]) || fabsf(p[k]) > 1e6f) finite = false;
        if (!finite) continue;
        if (veh_id != 0) {
            uint16_t vid = 0;
            if (br_safe_read(q + 0x54A, &vid, 2) && vid == veh_id)
                return off; /* сильный признак */
        }
        if (!best) best = off;
    }
    return best;
}
