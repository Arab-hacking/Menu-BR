/*
 * feat.cpp — реализация функций меню
 */
#include "br_common.h"
#include "detect.h"
#include <pthread.h>

#define FEAT_QUEUE_MAX 64

typedef struct {
    int cmd;
    int  len;
    uint8_t args[64];
} feat_cmd_t;

static feat_cmd_t g_q[FEAT_QUEUE_MAX];
static int        g_q_head = 0, g_q_tail = 0;
static pthread_mutex_t g_q_mtx = PTHREAD_MUTEX_INITIALIZER;

/* турбо */
static volatile int   g_turbo_on = 0;
static volatile float g_turbo_mult = 5.0f;
static float  g_prev_pos[3];
static float  g_dir[3] = {1, 0, 0};
static double g_prev_t = 0;
static int    g_have_prev = 0;

static uintptr_t g_spawned_veh = 0;

void br_feat_init(void)
{
    pthread_mutex_init(&g_q_mtx, 0);
    g_q_head = g_q_tail = 0;
}

void br_feat_shutdown(void)
{
    pthread_mutex_lock(&g_q_mtx);
    g_q_head = g_q_tail = 0;
    pthread_mutex_unlock(&g_q_mtx);
}

void br_feat_queue(int cmd, const void *args, int args_len)
{
    if (args_len > 64) args_len = 64;
    pthread_mutex_lock(&g_q_mtx);
    int next = (g_q_tail + 1) % FEAT_QUEUE_MAX;
    if (next != g_q_head) {
        g_q[g_q_tail].cmd = cmd;
        g_q[g_q_tail].len = args_len;
        if (args && args_len > 0) memcpy(g_q[g_q_tail].args, args, (size_t)args_len);
        g_q_tail = next;
    }
    pthread_mutex_unlock(&g_q_mtx);
}

/* ---------- вспомогательное ---------- */

/* адрес лежит в .text (RX) модуля игры? */
static bool in_game_text(uintptr_t addr)
{
    return br_mem_in(addr, 1, 1); /* базовая проверка; точная — по маппингам */
}

static uintptr_t local_ped_addr(uint16_t *id_out)
{
    int id = g_st.local_id_eff;
    if (id < 0) return 0;
    *id_out = (uint16_t)id;
    /* через найденный ped-указатель */
    if (g_st.player_ped_off) {
        uintptr_t obj = br_state_player_obj(id);
        if (obj) {
            uint64_t ped = 0;
            if (br_safe_read(obj + g_st.player_ped_off, &ped, 8) && ped != 0)
                return ped;
        }
    }
    /* через таблицу педов */
    if (g_st.ped_table)
        return g_st.ped_table + (uintptr_t)id * BR_PED_STRIDE;
    return 0;
}

static void do_teleport(const float *xyz)
{
    uint16_t id = 0;
    uintptr_t ped = local_ped_addr(&id);
    if (!ped) { LOGE("teleport: локальный пед не найден (id=%d)", g_st.local_id_eff); return; }
    if (!br_safe_write(ped + 0x38, xyz, 12)) { LOGE("teleport: запись запрещена"); return; }
    LOGI("teleport id=%d -> (%.1f, %.1f, %.1f)", id, xyz[0], xyz[1], xyz[2]);
    /* сбрасываем кэш позиции турбо */
    g_have_prev = 0;
}

static void do_engine(int pool_idx, int on)
{
    uintptr_t fn = (g_det.tbl ? g_det.tbl->veh_engine_method : 0);
    if (!fn) { LOGE("engine: адрес метода не известен для этой сборки"); return; }
    fn = g_det.base + fn;
    if (!in_game_text(fn)) { LOGE("engine: адрес вне .text"); return; }

    if (pool_idx < 0 || pool_idx >= BR_MAX_VEHICLES) return;
    uint64_t w = 0;
    if (!br_safe_read(g_st.pool_table + (uintptr_t)pool_idx * 8, &w, 8) || w == 0) return;
    uint16_t netid = 0;
    br_safe_read(w, &netid, 2);

    typedef void (*fn_t)(void *, int);
    ((fn_t)fn)((void *)w, on);
    LOGI("engine: veh netid=%u -> %s", netid, on ? "ON" : "OFF");
}

static void do_spawn(int model_id)
{
    uintptr_t fn = (g_det.tbl ? g_det.tbl->create_vehicle : 0);
    if (!fn) { LOGE("spawn: адрес фабрики не известен для этой сборки"); return; }
    fn = g_det.base + fn;
    if (!in_game_text(fn)) { LOGE("spawn: адрес вне .text"); return; }

    typedef void *(*fn_t)(int);
    void *v = ((fn_t)fn)(model_id);
    if (v) {
        g_spawned_veh = (uintptr_t)v;
        LOGI("spawn: model=%d -> CVehicle* %p", model_id, v);
    } else {
        LOGE("spawn: фабрика вернула NULL (model=%d)", model_id);
    }
}

static void do_enter_spawned(void)
{
    int id = g_st.local_id_eff;
    if (id < 0 || !g_spawned_veh) return;
    uintptr_t obj = br_state_player_obj(id);
    if (!obj) return;

    int voff = g_st.player_veh_off;
    if (!voff) { LOGE("enter: смещение veh ptr не найдено"); return; }

    /* все поля — из RE-документации (двусторонняя связь игрок<->машина) */
    br_safe_write(obj + voff, &g_spawned_veh, 8);            /* player+0x148/0x540 = CVehicle* */
    uint16_t vid = 0;
    br_safe_write(obj + 0x150, &vid, 2);                     /* id машины (локальный = 0) */
    uint32_t st = 0x13;
    br_safe_write(obj + 0x118, &st, 4);                      /* состояние "в машине" */
    br_safe_write(g_spawned_veh + 0xB8, &obj, 8);            /* CVehicle+0xB8 = RemotePlayer* */
    if (g_st.ped_table)
        br_safe_write(g_st.ped_table + (uintptr_t)id * BR_PED_STRIDE + 0x54A, &vid, 2);
    LOGI("enter: игрок %d -> spawned veh", id);
}

static void do_color(const float *rgb)
{
    int id = g_st.local_id_eff;
    if (id < 0) return;
    if (!g_st.color_table) { LOGE("color: таблица цветов не найдена"); return; }
    /* запись: f32 r,g,b в начале 16-байтной записи (формат RPC 401) */
    if (!br_safe_write(g_st.color_table + (uintptr_t)id * BR_COLOR_STRIDE, rgb, 12))
        LOGE("color: запись запрещена");
    else
        LOGI("color: id=%d rgb=(%.2f,%.2f,%.2f)", id, rgb[0], rgb[1], rgb[2]);
}

/* ---------- очередь ---------- */

void br_feat_drain(void)
{
    for (;;) {
        feat_cmd_t c;
        pthread_mutex_lock(&g_q_mtx);
        if (g_q_head == g_q_tail) { pthread_mutex_unlock(&g_q_mtx); return; }
        c = g_q[g_q_head];
        g_q_head = (g_q_head + 1) % FEAT_QUEUE_MAX;
        pthread_mutex_unlock(&g_q_mtx);

        switch (c.cmd) {
        case FEAT_TELEPORT:    if (c.len >= 12) do_teleport((const float *)c.args); break;
        case FEAT_SET_LOCAL: {
            if (c.len >= 4) {
                g_st.local_id = *(const int *)c.args;
                LOGI("local player id = %d", g_st.local_id);
            }
            break;
        }
        case FEAT_ENGINE: {
            const int *a = (const int *)c.args;
            if (c.len >= 8) do_engine(a[0], a[1]);
            break;
        }
        case FEAT_SPAWN:       if (c.len >= 4) do_spawn(*(const int *)c.args); break;
        case FEAT_ENTER_SPAWNED: do_enter_spawned(); break;
        case FEAT_SET_COLOR:   if (c.len >= 12) do_color((const float *)c.args); break;
        case FEAT_RESCAN:      br_detect_rerun(); break;
        default: break;
        }
    }
}

/* ---------- турбо-телепорт ---------- */

void br_feat_set_turbo(int on, float mult)
{
    g_turbo_on = on;
    if (mult < 0.1f) mult = 0.1f;
    if (mult > 100.0f) mult = 100.0f;
    g_turbo_mult = mult;
    LOGI("turbo: %s (x%.1f)", on ? "ON" : "OFF", mult);
}

int   br_feat_turbo_on(void)   { return g_turbo_on; }
float br_feat_turbo_mult(void) { return g_turbo_mult; }
uintptr_t br_feat_last_spawned(void) { return g_spawned_veh; }

void br_feat_turbo_tick(double dt_ms)
{
    if (!g_turbo_on) { g_have_prev = 0; return; }
    int id = g_st.local_id_eff;
    if (id < 0) return;
    float pos[3];
    if (!br_state_player_pos(id, pos)) { g_have_prev = 0; return; }

    if (g_have_prev) {
        double dx = pos[0] - g_prev_pos[0];
        double dy = pos[1] - g_prev_pos[1];
        double dz = pos[2] - g_prev_pos[2];
        double m = sqrt(dx*dx + dy*dy + dz*dz);
        if (m > 1e-4) {
            g_dir[0] = (float)(dx / m);
            g_dir[1] = (float)(dy / m);
            g_dir[2] = (float)(dz / m);
        }
    }
    g_prev_pos[0] = pos[0]; g_prev_pos[1] = pos[1]; g_prev_pos[2] = pos[2];
    g_prev_t = br_now_ms();
    g_have_prev = 1;

    /* базовая скорость: текущая или минимум 15 м/с */
    float base = g_st.players[id].speed;
    if (base < 15.0f) base = 15.0f;
    float step = base * g_turbo_mult * (float)(dt_ms / 1000.0);
    if (step > 500.0f) step = 500.0f;

    pos[0] += g_dir[0] * step;
    pos[1] += g_dir[1] * step;
    pos[2] += g_dir[2] * step;

    uint16_t lid = 0;
    uintptr_t ped = local_ped_addr(&lid);
    if (ped) br_safe_write(ped + 0x38, pos, 12);
}
