/*
 * netmon.cpp — реализация монитора (см. netmon.h)
 */
#include "br_common.h"
#include "detect.h"
#include "netmon.h"
#include "func_hook.h"
#include <pthread.h>
#include <math.h>

#define RING_SIZE 512

static net_event_t g_ring[RING_SIZE];
static volatile int g_head = 0;          /* индекс следующего записи */
static volatile int g_running = 0;
static double      g_start_ms = 0;

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_count_send[600];
static uint64_t g_count_rpc[600];
static uint64_t g_total_send, g_total_rpc;

/* оригиналы (trampoline'ы) */
static void (*g_send_orig)(void *, int, int, int) = 0;
static void (*g_rpc_orig)(int, void *, int, int, int) = 0;

void br_netmon_init(void)
{
    pthread_mutex_lock(&g_mtx);
    g_head = 0;
    memset(g_count_send, 0, sizeof(g_count_send));
    memset(g_count_rpc, 0, sizeof(g_count_rpc));
    g_total_send = g_total_rpc = 0;
    pthread_mutex_unlock(&g_mtx);
    g_running = 0;
    g_send_orig = 0;
    g_rpc_orig = 0;
}

void br_netmon_stop(void)
{
    g_running = 0;
}

int br_netmon_attach(uintptr_t base, uintptr_t send_va, uintptr_t rpc_va)
{
    g_running = 1;
    g_start_ms = br_now_ms();
    int ok = 0;

    if (send_va) {
        void *orig = 0;
        if (br_fhook_install((void *)(base + send_va),
                             (void *)br_netmon_send_hook, &orig) == 0) {
            g_send_orig = (void (*)(void *, int, int, int))orig;
            ok = 1;
        }
    }
    if (rpc_va) {
        void *orig = 0;
        if (br_fhook_install((void *)(base + rpc_va),
                             (void *)br_netmon_rpc_hook, &orig) == 0) {
            g_rpc_orig = (void (*)(int, void *, int, int, int))orig;
            ok = 1;
        }
    }
    if (ok) LOGI("netmon: встал (send=%s rpc=%s)",
                g_send_orig ? "ok" : "нет", g_rpc_orig ? "ok" : "нет");
    return ok;
}

static void record(int kind, uint16_t id, uint16_t size, const uint8_t *head)
{
    if (!g_running) return;
    net_event_t *slot = &g_ring[g_head & (RING_SIZE - 1)];
    slot->kind = (uint8_t)kind;
    slot->id = id;
    slot->size = size;
    slot->t = (uint32_t)(br_now_ms() - g_start_ms);
    if (head) memcpy(slot->head, head, 16);
    else memset(slot->head, 0, 16);
    g_head++;

    pthread_mutex_lock(&g_mtx);
    if (id < 600) (kind ? g_count_rpc : g_count_send)[id]++;
    if (kind) g_total_rpc++; else g_total_send++;
    pthread_mutex_unlock(&g_mtx);
}

/* ---------- хуки ---------- */

void br_netmon_send_hook(void *bs, int prio, int rel, int chan)
{
    /* сначала — запись, потом оригинал (монитор не влияет на геймплей) */
    if (bs) {
        int32_t bits = 0;
        uint8_t *data = 0;
        br_safe_read((uintptr_t)bs + 0x00, &bits, 4);
        br_safe_read((uintptr_t)bs + 0x10, &data, 8);
        if (bits > 0 && data) {
            int size = bits / 8;
            if (size > 255) size = 255;
            uint8_t head[16] = {0};
            if (size >= 1) br_safe_read((uintptr_t)data, head, (size < 16) ? (size_t)size : 16);
            record(0, head[0], (uint16_t)size, head);
        }
    }
    if (g_send_orig) g_send_orig(bs, prio, rel, chan);
}

void br_netmon_rpc_hook(int id, void *bs, int prio, int rel, int chan)
{
    if (bs) {
        int32_t bits = 0;
        uint8_t *data = 0;
        br_safe_read((uintptr_t)bs + 0x00, &bits, 4);
        br_safe_read((uintptr_t)bs + 0x10, &data, 8);
        int size = (bits > 0) ? bits / 8 : 0;
        if (size > 255) size = 255;
        uint8_t head[16] = {0};
        if (data && size >= 1) br_safe_read((uintptr_t)data, head, (size < 16) ? (size_t)size : 16);
        record(1, (uint16_t)id, (uint16_t)size, head);
    }
    if (g_rpc_orig) g_rpc_orig(id, bs, prio, rel, chan);
}

/* ---------- чтение (UI-тред) ---------- */

int br_netmon_snapshot(net_event_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < max && n < RING_SIZE; i++) {
        int idx = (g_head - 1 - i) & (RING_SIZE - 1);
        out[n++] = g_ring[idx];
    }
    return n;
}

uint64_t br_netmon_count(int kind, int id)
{
    if (id < 0 || id >= 600) return 0;
    pthread_mutex_lock(&g_mtx);
    uint64_t c = kind ? g_count_rpc[id] : g_count_send[id];
    pthread_mutex_unlock(&g_mtx);
    return c;
}

uint64_t br_netmon_total(int kind)
{
    pthread_mutex_lock(&g_mtx);
    uint64_t c = kind ? g_total_rpc : g_total_send;
    pthread_mutex_unlock(&g_mtx);
    return c;
}

/* ---------- статус RakClient через vtable ----------
 * g_pRakClient (глобал из таблицы) -> vptr -> slot -> вызов */

static uintptr_t rak_client_ptr(void)
{
    if (!g_det.tbl || !g_det.tbl->rak_client_glob) return 0;
    uint64_t p = 0;
    if (!br_safe_read(g_det.base + g_det.tbl->rak_client_glob, &p, 8)) return 0;
    return p;
}

static int rak_vcall(uintptr_t slot, int *out)
{
    uintptr_t rc = rak_client_ptr();
    if (!rc) return -1;
    uint64_t vptr = 0;
    if (!br_safe_read(rc, &vptr, 8) || vptr < 0x10000) return -1;
    uint64_t fn = 0;
    if (!br_safe_read(vptr + slot, &fn, 8) || fn < 0x10000) return -1;
    if (!br_mem_in(fn, 4, 1)) return -1;
    int r = ((int (*)(uintptr_t))fn)(rc);
    *out = r;
    return 0;
}

int br_netmon_ping_avg(void)
{
    if (!g_det.tbl || !g_det.tbl->rak_client_glob) return -1;
    int v = -1;
    return rak_vcall(0x68, &v) == 0 ? v : -1;
}

int br_netmon_mtu(void)
{
    if (!g_det.tbl || !g_det.tbl->rak_client_glob) return -1;
    int v = -1;
    return rak_vcall(0xF0, &v) == 0 ? v : -1;
}

int br_netmon_connected(void)
{
    if (!g_det.tbl || !g_det.tbl->rak_client_glob) return -1;
    int v = -1;
    return rak_vcall(0x90, &v) == 0 ? v : -1;
}
