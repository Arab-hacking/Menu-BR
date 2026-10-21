/*
 * state.h — модель игрового состояния (снимок по данным из RE-документации)
 */
#ifndef BR_STATE_H
#define BR_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t id;
    float    pos[3];      /* из таблицы педов (+0x38/0x3C/0x40) */
    float    speed;       /* м/с, EMA по дельте позиции */
    uint8_t  state;       /* +0x118: 19 = в машине */
    uint16_t veh_id;      /* +0x150 */
    int      veh_class;   /* класс машины (через veh ptr), -1 = нет */
    uint8_t  online;      /* слот занят */
} br_player_t;

typedef struct {
    uint16_t net_id;
    int      veh_class;   /* CVehicle+0x430 */
    float    pos[3];      /* позиция пассажира-водителя (прокси) */
    float    speed;       /* м/с */
    uint16_t occupant_id; /* 0 = никто (не определён) */
    uint8_t  valid;
} br_vehicle_t;

typedef struct {
    /* адреса (абсолютные, base уже прибавлен) */
    uintptr_t pool_table;   /* VehiclePool: 2000 × 8B */
    uintptr_t mgr_slots;    /* PlayerManager: +8+id*8 */
    uintptr_t ped_table;    /* CPed[]: шаг 0xF8 */
    uintptr_t color_table;  /* 16B/игрок */

    int      player_veh_off;  /* смещение CVehicle* в RemotePlayer */
    int      player_ped_off;  /* смещение CPed* в RemotePlayer (0 = таблица педов) */
    int      build;           /* br_build */
    bool     addr_ok;         /* минимум пул+менеджер валидны */

    /* сэмплируемое состояние (обновляется tr-ом сэмплера) */
    br_player_t  players[BR_MAX_PLAYERS];
    int          players_online;

    br_vehicle_t vehicles[BR_MAX_VEHICLES];
    int          vehicles_count;

    int      local_id;        /* -1 = авто */
    int      local_id_eff;    /* фактически используемый */

    double   last_sample_ms;
} br_state_t;

extern br_state_t g_st;

void br_state_reset(void);

/* Позиция игрока (общая функция: таблица педов / ped ptr / +0x94). */
bool br_state_player_pos(int id, float out[3]);

/* Адрес объекта игрока (0 если офлайн). */
uintptr_t br_state_player_obj(int id);

/* Адрес CVehicle* (0 если не в машине / не найдено). */
uintptr_t br_state_player_veh(int id);

/* Пуск тред-сэмплера (~30 Гц). 0 = ок. */
int  br_state_start_sampler(void);
void  br_state_stop_sampler(void);

#endif
