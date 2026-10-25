/*
 * off.h — известные адреса по сборкам
 *
 * 16.110.18323: ВСЕ значения сверены с реальным бинарником (артефакт пользователя):
 *   - глобалы — через .rela.dyn (R_AARCH64_RELATIVE),
 *   - GetByNetId 0x1A1C1C8 — дизассемблировано: `and w8,w1,#0xffff; cmp w8,#0x7cf;
 *     ldr x8,[x0,x8,lsl#3]` — идентично описанию,
 *   - диспетчер 0x19D4664 — найден по ассерт-строке
 *     "Invalid packetId for sync struct" (7 ссылок внутри функции),
 *     switch по id пакетов, x21 = глобал 0x49B7E0, packetId = [x21+0xEB8]
 *     => глобал packetId = 0x49C698,
 *   - build-id = 805be086548eb236ee469e88c11d5d3e66cfe1ba.
 *
 * 16.106.17587: адреса из документации (VEHICLE_STRUCTURE.md / PLAYER_STRUCTURE.md);
 *   бинарника под рукой нет — таблица используется как первичная и всегда
 *   перепроверяется структурными пробами.
 *
 * ВАЖНО: VA — внутри ELF (без ASLR). В процессе: addr = base + VA.
 */
#ifndef BR_OFF_H
#define BR_OFF_H

#include <stdint.h>

enum br_build {
    BR_BUILD_AUTO   = 0,
    BR_BUILD_16106  = 1,
    BR_BUILD_16110  = 2,
    BR_BUILD_16111  = 3,
    BR_BUILD_CUSTOM = 4, /* ручные адреса */
};

typedef struct {
    const char *name;

    /* build-id (SHA1, 20B); все нули = неизвестен */
    uint8_t  build_id[20];

    /* глобалы (VA) */
    uintptr_t pool_glob;        /* VehiclePool*  (таблица 2000 × 8B) */
    uintptr_t player_mgr_glob;  /* PlayerManager* (слоты: mgr+8+id*8, id<=1503) */
    uintptr_t ped_table_glob;   /* таблица педов, шаг 0xF8, координаты +0x38/0x3C/0x40 */
    uintptr_t color_table_glob; /* 16B на игрока (RPC 401) */

    /* функции (VA) */
    uintptr_t dispatcher;       /* диспетчер синхро-пакетов */
    uintptr_t get_by_net_id;    /* VehiclePool::GetByNetId(u16) */
    uintptr_t get_player_by_id; /* GetPlayerById(id<=1503) */
    uintptr_t create_vehicle;   /* фабрика CreateVehicle(modelIndex) */
    uintptr_t veh_engine_method;/* метод CVehicle(bool) — двигатель/свет (гипотеза) */

    /* поля структур */
    int  player_veh_ptr_off;    /* указатель CVehicle* в объекте игрока */
    int  player_veh_flag_off;   /* флаг "в машине" (16.110: +0x548) */
    int  player_state_off;      /* состояние: 19 = в машине (+0x118) */
    int  player_veh_id_off;     /* u16 id машины у игрока (+0x150) */
    int  veh_occupant_off;      /* CVehicle+0xB8 = RemotePlayer* */
    int  veh_class_off;         /* CVehicle+0x430 = класс */
    int  ped_veh_id_off;        /* ped+0x54A = u16 id машины */

    /* сетевой монитор: глобал с текущим packetId (0 = неизвестен) */
    uintptr_t sync_id_glob;

    /* сигнатуры для автопоиска (паттерн + маска; 0x00 в маске = дик) */
    uint8_t sig_getby[8];
    uint8_t sig_getby_mask[8];
    uint8_t sig_disp[36];
    uint8_t sig_disp_mask[36];

    /* ---- 16.111: новое (хвост структуры, старые таблицы = 0) ---- */
    int  player_ped_off_local;   /* CPed* в CLocalPlayer (0 = объект и есть пед) */
    int  player_ped_off_remote;  /* CPed* в CRemotePlayer (0 = объект и есть пед) */
    uintptr_t rak_client_glob;   /* RakClientInterface* (0 = неизвестен) */
    uintptr_t jni_step_va;       /* JNILib_step — кадр GLThread (рендер-хук) */
    uintptr_t jni_touch_va;      /* JNILib_multiTouchEvent — хук касаний */
    uintptr_t rak_send_va;       /* Send(BitStream*,prio,rel,chan) thunk */
    uintptr_t rak_rpc_va;        /* RPC(id,BitStream*,prio,rel,chan) thunk */
    uintptr_t obj_pool_glob;     /* ObjectPool (4096 × 8B) для ESP */
} br_build_table_t;

extern const br_build_table_t BR_TABLES[]; /* индекс = br_build (1,2) */
extern const int BR_TABLE_COUNT;

/* Имена для UI */
static const char *br_build_name(int b)
{
    switch (b) {
    case BR_BUILD_16106: return "16.106.17587";
    case BR_BUILD_16110: return "16.110.18323";
    case BR_BUILD_16111: return "16.111.19050";
    case BR_BUILD_CUSTOM:return "Custom";
    default:             return "Auto";
    }
}

#endif
