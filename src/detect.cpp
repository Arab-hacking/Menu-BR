/*
 * detect.cpp — определение сборки и адресов + сетевой монитор
 */
#include "br_common.h"
#include "detect.h"
#include <pthread.h>

br_detect_t g_det;
br_monitor_t g_mon;

/* ---------- валидация таблицы ---------- */

struct verify_res {
    bool pool_ok, mgr_ok, ped_ok, color_ok;
    int  pool_valid, mgr_valid, online, ped_valid;
};

static void verify_table(int tidx, struct verify_res *r)
{
    const br_build_table_t *t = &BR_TABLES[tidx];
    uintptr_t base = g_det.base;
    memset(r, 0, sizeof(*r));

    r->pool_ok = t->pool_glob && br_pool_plausible(base + t->pool_glob, 0);
    r->mgr_ok  = t->player_mgr_glob && br_validate_manager(base + t->player_mgr_glob, &r->mgr_valid, &r->online);
    r->ped_ok  = t->ped_table_glob && br_validate_ped_table(base + t->ped_table_glob, &r->ped_valid);
    r->color_ok= t->color_table_glob && br_mem_in(base + t->color_table_glob, 16 * 64, 1);
    if (r->pool_ok) br_validate_pool(base + t->pool_glob, &r->pool_valid, 0);
}

/* ---------- применение результатов ---------- */

void br_detect_apply(void)
{
    br_state_t *s = &g_st;
    uintptr_t base = g_det.base;
    const br_build_table_t *t = g_det.tbl;

    /* --- пул --- */
    s->pool_table = 0;
    if (g_det.custom_pool) {
        uint64_t p = 0;
        if (br_safe_read(g_det.custom_pool, &p, 8)) s->pool_table = p;
        snprintf(g_det.src_pool, sizeof(g_det.src_pool), "manual");
    } else if (t && t->pool_glob) {
        uint64_t p = 0;
        if (br_safe_read(base + t->pool_glob, &p, 8)) { s->pool_table = p; snprintf(g_det.src_pool, sizeof(g_det.src_pool), "table+verify"); }
        else if (g_det.probe.pool_found) { s->pool_table = g_det.probe.pool_table; snprintf(g_det.src_pool, sizeof(g_det.src_pool), "probe"); }
    } else if (g_det.probe.pool_found) {
        s->pool_table = g_det.probe.pool_table;
        snprintf(g_det.src_pool, sizeof(g_det.src_pool), "probe");
    }

    /* --- менеджер --- */
    s->mgr_slots = 0;
    if (g_det.custom_mgr) {
        s->mgr_slots = g_det.custom_mgr + 8;
        snprintf(g_det.src_mgr, sizeof(g_det.src_mgr), "manual");
    } else if (t && t->player_mgr_glob) {
        s->mgr_slots = base + t->player_mgr_glob + 8;
        snprintf(g_det.src_mgr, sizeof(g_det.src_mgr), "table+verify");
    } else if (g_det.probe.mgr_found) {
        s->mgr_slots = g_det.probe.mgr_ptr + 8;
        snprintf(g_det.src_mgr, sizeof(g_det.src_mgr), "probe");
    }

    /* --- таблица педов --- */
    s->ped_table = 0;
    if (g_det.custom_ped) {
        s->ped_table = g_det.custom_ped;
        snprintf(g_det.src_ped, sizeof(g_det.src_ped), "manual");
    } else if (t && t->ped_table_glob) {
        s->ped_table = base + t->ped_table_glob;
        snprintf(g_det.src_ped, sizeof(g_det.src_ped), "table");
    } else if (g_det.probe.ped_found) {
        s->ped_table = g_det.probe.ped_ptr;
        snprintf(g_det.src_ped, sizeof(g_det.src_ped), "probe");
    }

    /* --- цвета --- */
    s->color_table = 0;
    if (g_det.custom_color) s->color_table = g_det.custom_color;
    else if (t && t->color_table_glob) s->color_table = base + t->color_table_glob;

    /* --- смещения --- */
    s->player_veh_off = (t && t->player_veh_ptr_off) ? t->player_veh_ptr_off : g_det.probe.player_veh_off;
    s->player_ped_off = g_det.probe.player_ped_off;

    /* авто-поиск player_veh_off: первый игрок в машине + обратная ссылка 0xB8 */
    if (!s->player_veh_off && s->mgr_slots) {
        for (int id = 0; id < BR_MAX_PLAYERS; id++) {
            uintptr_t obj = br_state_player_obj(id);
            if (!obj) continue;
            uint32_t st = 0;
            if (br_safe_read(obj + 0x118, &st, 4) && st == 0x13) {
                int off = br_find_player_veh_off(obj);
                if (off) {
                    s->player_veh_off = off;
                    LOGI("player_veh_off найден: 0x%x (игрок %d)", off, id);
                    break;
                }
            }
        }
    }

    s->build = g_det.build;
    s->addr_ok = (s->pool_table != 0) && (s->mgr_slots != 0);

    LOGI("адреса: pool=%s mgr=%s ped=%s | veh_off=0x%x ped_off=0x%x",
         g_det.src_pool, g_det.src_mgr, g_det.src_ped,
         s->player_veh_off, s->player_ped_off);
}

/* ---------- сигнатурный поиск ---------- */

static void sig_scan(int tidx)
{
    const br_build_table_t *t = &BR_TABLES[tidx];
    if (!t->sig_getby[0] || !t->sig_getby_mask[0]) return;
    /* сканируем весь .text (диапазон: base+0x4000 .. base+0x2300000, с запасом) */
    uintptr_t first = 0;
    int n = br_elf_scan(&g_det.elf, 0x4000, 0x2300000,
                        t->sig_getby, t->sig_getby_mask, 8, &first);
    if (n == 1 && first) {
        LOGI("GetByNetId найден сигнатурой: 0x%lx (build %s)", (unsigned long)first, t->name);
        if (g_det.custom_getby == 0) g_det.custom_getby = g_det.base + first;
    } else if (n > 1) {
        LOGW("GetByNetId: %d совпадений сигнатуры — игнор", n);
    }
}

/* ---------- главный поиск ---------- */

static int do_detect(void)
{
    br_maps_refresh();
    g_det.base = br_find_module("libblackrussia-client.so");
    if (!g_det.base) {
        LOGE("libblackrussia-client.so не найден в процессах");
        return BRMENU_ERR_NO_GAME;
    }
    LOGI("база игры: 0x%lx", (unsigned long)g_det.base);
    br_elf_load(&g_det.elf, g_det.base);
    if (g_det.elf.has_build_id) {
        char bid[41] = "";
        for (int i = 0; i < 20; i++) sprintf(bid + i * 2, "%02x", g_det.elf.build_id[i]);
        LOGI("build-id: %s", bid);
    }

    int forced = g_cfg.build;

    /* 1) build-id */
    if (forced == BR_BUILD_AUTO && g_det.elf.has_build_id) {
        for (int t = 1; t < BR_TABLE_COUNT; t++) {
            if (!BR_TABLES[t].build_id[0]) continue;
            if (memcmp(BR_TABLES[t].build_id, g_det.elf.build_id, 20) == 0) {
                LOGI("build-id совпадает: %s", BR_TABLES[t].name);
                forced = t;
                break;
            }
        }
    }

    /* 2) форс/совпадение: проверяем таблицу */
    int found = 0;
    if (forced != BR_BUILD_AUTO && forced >= 1 && forced < BR_TABLE_COUNT) {
        struct verify_res v;
        verify_table(forced, &v);
        if (v.pool_ok && v.mgr_ok) {
            g_det.build = forced;
            g_det.tbl = &BR_TABLES[forced];
            found = 1;
            LOGI("таблица %s подтверждена (machin=%d online=%d)",
                 g_det.tbl->name, v.pool_valid, v.online);
        } else {
            LOGW("таблица %s НЕ подтверждена (pool=%d mgr=%d) — ищем иначе",
                 BR_TABLES[forced].name, v.pool_ok, v.mgr_ok);
        }
    }

    /* 3) перебор остальных таблиц (структуры часто сохраняются) */
    if (!found) {
        for (int t = 1; t < BR_TABLE_COUNT; t++) {
            if (t == forced) continue;
            struct verify_res v;
            verify_table(t, &v);
            if (v.pool_ok && v.mgr_ok) {
                g_det.build = t;
                g_det.tbl = &BR_TABLES[t];
                found = 1;
                LOGI("таблица %s подтверждена перебором (machin=%d online=%d)",
                     g_det.tbl->name, v.pool_valid, v.online);
                break;
            }
        }
    }

    /* 4) сигнатуры (даже если таблица нашлась — для доп. адресов) */
    for (int t = 1; t < BR_TABLE_COUNT; t++) sig_scan(t);

    /* 5) полный структурный проба (если ядро не найдено) */
    memset(&g_det.probe, 0, sizeof(g_det.probe));
    if (!found) {
        LOGI("запускаю структурный проба...");
        br_probe_all(g_det.base, &g_det.probe);
        if (g_det.probe.pool_found || g_det.probe.mgr_found) {
            g_det.build = BR_BUILD_CUSTOM;
            g_det.tbl = 0;
            LOGI("проба: pool=%d mgr=%d ped=%d",
                 g_det.probe.pool_found, g_det.probe.mgr_found, g_det.probe.ped_found);
        } else {
            LOGE("проба ничего не нашла — используйте вкладку Net (ручные адреса)");
        }
    }

    if (g_det.build == BR_BUILD_CUSTOM && !g_det.tbl) {
        /* для проба/ручных — смещения по умолчанию 16.110 */
        static br_build_table_t custom_tbl;
        memcpy(&custom_tbl, &BR_TABLES[BR_BUILD_16110], sizeof(custom_tbl));
        custom_tbl.name = "detected";
        custom_tbl.pool_glob = custom_tbl.player_mgr_glob = custom_tbl.ped_table_glob = 0;
        g_det.tbl = &custom_tbl;
    }

    br_detect_apply();
    return g_st.addr_ok ? BRMENU_OK : BRMENU_ERR_GENERIC;
}

int br_detect_init(void)
{
    int rc = do_detect();
    if (rc == BRMENU_OK) {
        br_monitor_start();
    }
    return rc;
}

void br_detect_rerun(void)
{
    LOGI("рескан...");
    int rc = do_detect();
    LOGI("рескан: %s", rc == BRMENU_OK ? "OK" : "FAIL");
}

/* ---------- сетевой монитор ---------- */

static pthread_t g_mon_tid;
static volatile int g_mon_run = 0;

static void *mon_thread(void *arg)
{
    (void)arg;
    const br_build_table_t *t = g_det.tbl;
    uintptr_t id_addr = 0;
    if (t && t->sync_id_glob)
        id_addr = g_det.base + t->sync_id_glob;

    static uint64_t buckets[3][40];
    int bidx = 0;
    double sec_acc = 0;
    uint32_t last = 0;
    bool have_last = false;
    double last_tick = br_now_ms();

    for (;;) {
        if (!g_mon_run) return 0;
        usleep(2000); /* 500 Гц */

        double now = br_now_ms();
        double dt = (now - last_tick) / 1000.0;
        last_tick = now;
        sec_acc += dt;

        /* перекат секунд */
        if (sec_acc >= 1.0) {
            sec_acc -= 1.0;
            bidx = (bidx + 1) % 3;
            memset(buckets[bidx], 0, sizeof(buckets[bidx]));
            for (int i = 0; i < 40; i++)
                g_mon.pps[i] = ((double)buckets[(bidx + 1) % 3][i] +
                                (double)buckets[(bidx + 2) % 3][i]) * 0.5;
        }

        if (!id_addr) { g_mon.running = 0; continue; }

        uint32_t v = 0;
        if (!br_safe_read(id_addr, &v, 4)) { g_mon.running = 0; continue; }
        g_mon.running = 1;
        g_mon.last_id = (int)v;

        if (have_last && v != last && v != 0 && v < 40) {
            buckets[bidx][v]++;
            g_mon.total[v]++;
        }
        last = v;
        have_last = true;
    }
    return 0;
}

void br_monitor_start(void)
{
    if (g_mon_run) return;
    memset(&g_mon, 0, sizeof(g_mon));
    g_mon.last_id = -1;
    g_mon.started_ms = br_now_ms();
    g_mon_run = 1;
    if (pthread_create(&g_mon_tid, 0, mon_thread, 0) == 0)
        pthread_detach(g_mon_tid);
    else
        g_mon_run = 0;
}

void br_monitor_stop(void)
{
    g_mon_run = 0;
}
