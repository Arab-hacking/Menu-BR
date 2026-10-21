/*
 * state.cpp — сэмплирование состояния игры
 */
#include "br_common.h"
#include <pthread.h>
#include <unistd.h>

br_state_t g_st;

static void ema_speed(br_player_t *p, float *prev, double prev_t, float *np, double t)
{
    double dt = (t - prev_t) / 1000.0;
    if (dt <= 0.001 || dt > 1.0) { prev[0]=np[0]; prev[1]=np[1]; prev[2]=np[2]; return; }
    double dx = np[0]-prev[0], dy = np[1]-prev[1], dz = np[2]-prev[2];
    float v = (float)sqrt(dx*dx + dy*dy + dz*dz) / (float)dt;
    if (v > 500.0f) v = 500.0f;
    if (p->speed < 0.01f) p->speed = v;
    else p->speed = (float)(p->speed * 0.75 + v * 0.25);
}

void br_state_reset(void)
{
    memset(&g_st, 0, sizeof(g_st));
    g_st.local_id = -1;
    g_st.local_id_eff = -1;
    for (int i = 0; i < BR_MAX_PLAYERS; i++) g_st.players[i].veh_class = -1;
}

uintptr_t br_state_player_obj(int id)
{
    if (id < 0 || id >= BR_MAX_PLAYERS) return 0;
    if (!g_st.mgr_slots) return 0;
    uint64_t obj = 0;
    if (!br_safe_read(g_st.mgr_slots + (uintptr_t)id * 8, &obj, 8)) return 0;
    return obj;
}

bool br_state_player_pos(int id, float out[3])
{
    if (id < 0 || id >= BR_MAX_PLAYERS) return false;

    /* приоритет: CPed* внутри объекта игрока (если смещение найдено) */
    if (g_st.player_ped_off) {
        uintptr_t obj = br_state_player_obj(id);
        if (obj) {
            uint64_t ped = 0;
            if (br_safe_read(obj + g_st.player_ped_off, &ped, 8) && ped != 0) {
                if (br_readf(ped + 0x38, out, 3)) {
                    bool fin = true;
                    for (int k = 0; k < 3; k++)
                        if (!isfinite(out[k])) fin = false;
                    if (fin) return true;
                }
            }
        }
    }
    /* иначе: общая таблица педов (шаг 0xF8, координаты +0x38) */
    if (g_st.ped_table) {
        if (br_readf(g_st.ped_table + (uintptr_t)id * BR_PED_STRIDE + 0x38, out, 3)) {
            bool fin = true;
            for (int k = 0; k < 3; k++)
                if (!isfinite(out[k])) fin = false;
            if (fin) return true;
        }
    }
    return false;
}

uintptr_t br_state_player_veh(int id)
{
    if (id < 0 || id >= BR_MAX_PLAYERS) return 0;
    uintptr_t obj = br_state_player_obj(id);
    if (!obj) return 0;
    int off = g_st.player_veh_off;
    if (!off) return 0;
    uint64_t v = 0;
    if (!br_safe_read(obj + off, &v, 8)) return 0;
    return v;
}

/* ---------- сэмплер ---------- */

static void sample_players(double now_ms)
{
    int online = 0;
    static float prev_pos[BR_MAX_PLAYERS][3];
    static double prev_t[BR_MAX_PLAYERS];

    for (int id = 0; id < BR_MAX_PLAYERS; id++) {
        br_player_t *p = &g_st.players[id];
        uintptr_t obj = br_state_player_obj(id);
        if (obj == 0) {
            p->online = 0;
            p->speed = 0;
            continue;
        }
        uint32_t st = 0;
        uint16_t vid = 0;
        br_safe_read(obj + 0x118, &st, 4);
        br_safe_read(obj + 0x150, &vid, 2);

        p->id = (uint16_t)id;
        p->online = 1;
        p->state = (uint8_t)st;
        p->veh_id = vid;
        p->veh_class = -1;

        float pos[3];
        if (br_state_player_pos(id, pos)) {
            for (int k = 0; k < 3; k++) p->pos[k] = pos[k];
            if (prev_t[id] > 0) ema_speed(p, &prev_pos[id][0], prev_t[id], pos, now_ms);
            for (int k = 0; k < 3; k++) prev_pos[id][k] = pos[k];
            prev_t[id] = now_ms;
        } else {
            p->speed = 0;
            prev_t[id] = 0;
        }

        if (st == 0x13 && vid) {
            uintptr_t v = br_state_player_veh(id);
            if (v) {
                uint32_t cls = 0;
                if (br_safe_read(v + 0x430, &cls, 4) && cls <= 12)
                    p->veh_class = (int)cls;
            }
        }
        online++;
    }
    g_st.players_online = online;
}

/* обратный поиск: игрок по указателю RemotePlayer* */
static int player_by_obj(uint64_t obj)
{
    if (!obj) return -1;
    for (int id = 0; id < BR_MAX_PLAYERS; id++) {
        uintptr_t o = br_state_player_obj(id);
        if (o == obj) return id;
    }
    return -1;
}

static void sample_vehicles(double now_ms)
{
    int count = 0;
    static float vprev[BR_MAX_VEHICLES][3];
    static double vprev_t[BR_MAX_VEHICLES];

    if (!g_st.pool_table) return;

    for (int i = 0; i < BR_MAX_VEHICLES; i++) {
        uint64_t w = 0;
        if (!br_safe_read(g_st.pool_table + (uintptr_t)i * 8, &w, 8)) continue;
        if (w == 0) continue;
        uint16_t netid = 0;
        uint32_t cls = 0;
        if (!br_safe_read(w, &netid, 2)) continue;
        if (netid >= BR_MAX_VEHICLES) continue;
        br_safe_read(w + 0x430, &cls, 4);

        br_vehicle_t *v = &g_st.vehicles[i];
        v->net_id = netid;
        v->veh_class = (cls <= 12) ? (int)cls : 0;
        v->valid = 1;
        count++;

        /* пассажир → позиция/скорость машины = позиция/скорость водителя */
        v->occupant_id = 0;
        v->pos[0] = v->pos[1] = v->pos[2] = 0;
        v->speed = 0;
        uint64_t occ = 0;
        if (br_safe_read(w + 0xB8, &occ, 8)) {
            int pid = player_by_obj(occ);
            if (pid >= 0) {
                v->occupant_id = (uint16_t)pid;
                if (br_state_player_pos(pid, v->pos)) {
                    v->speed = g_st.players[pid].speed;
                    vprev_t[i] = now_ms;
                    for (int k = 0; k < 3; k++) vprev[i][k] = v->pos[k];
                }
            }
        }
    }
    g_st.vehicles_count = count;
}

/* локальный игрок: авто = первый онлайн с валидной позицией */
static void resolve_local(void)
{
    int lid = g_st.local_id;
    if (lid >= 0 && lid < BR_MAX_PLAYERS && g_st.players[lid].online) {
        g_st.local_id_eff = lid;
        return;
    }
    g_st.local_id_eff = -1;
    for (int id = 0; id < BR_MAX_PLAYERS; id++) {
        if (g_st.players[id].online) { g_st.local_id_eff = id; break; }
    }
}

static void *sample_thread(void *arg)
{
    (void)arg;
    double last = 0;
    for (;;) {
        double now = br_now_ms();
        if (now - last >= 33.0) {
            sample_players(now);
            sample_vehicles(now);
            resolve_local();
            last = now;
        }
        usleep(8000);
    }
    return 0;
}

static pthread_t g_sample_tid;
static volatile int g_sample_run = 0;

int br_state_start_sampler(void)
{
    if (g_sample_run) return 0;
    g_sample_run = 1;
    if (pthread_create(&g_sample_tid, 0, sample_thread, 0) != 0) {
        g_sample_run = 0;
        return -1;
    }
    pthread_detach(g_sample_tid);
    return 0;
}
