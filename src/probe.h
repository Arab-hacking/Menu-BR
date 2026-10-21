/*
 * probe.h — структурные пробы и валидация известных адресов
 *
 * Идея: адреса меняются между сборками, но СТРУКТУРЫ устойчивы:
 *   - пул машин: указатель на таблицу 2000 × 8B; [обёртка+0] = net id (u16 < 2000),
 *     [обёртка+0x430] = класс (0..12);
 *   - менеджер игроков: слоты mgr+8+id*8 (id<=1503), [объект+0x118] = состояние;
 *   - таблица педов: шаг 0xF8, координаты f32×3 по +0x38.
 * Все проверки — только через safe_read; процессу игры ничего не ломается.
 */
#ifndef BR_PROBE_H
#define BR_PROBE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool      pool_found;
    uintptr_t pool_ptr;        /* сам указатель в .bss игры (адрес глобала) */
    uintptr_t pool_table;      /* куда он указывает */
    int       pool_valid_slots;

    bool      mgr_found;
    uintptr_t mgr_ptr;
    int       mgr_valid_ids;
    int       players_online;

    bool      ped_found;
    uintptr_t ped_ptr;
    int       ped_valid_ids;

    bool      color_found;
    uintptr_t color_ptr;

    /* смещения, найденные в объекте игрока (0 = не найдено) */
    int       player_veh_off;   /* CVehicle* внутри RemotePlayer */
    int       player_ped_off;   /* CPed* внутри RemotePlayer (0 = используем таблицу педов) */
} br_probe_t;

/* Валидация (используется и для проверки адресов из таблиц).
 * pool_ptr — адрес, В КОТОРОМ лежит указатель на таблицу (глобал).
 * Строгая: требует >=2 валидных машин (для проба). */
bool br_validate_pool(uintptr_t pool_glob, int *valid_slots, uintptr_t *table_out);

/* Мягкая: таблица читаемая, все выборочные слоты либо пустые, либо валидные
 * (пустой мир тоже ок). Для проверки адресов из таблиц. */
bool br_pool_plausible(uintptr_t pool_glob, uintptr_t *table_out);
bool br_validate_manager(uintptr_t mgr_glob, int *valid_ids, int *online_out);
bool br_validate_ped_table(uintptr_t ped_glob, int *valid_out);

/* Поиск по данным областям модуля (медленно, ~0.1-0.5 c; вызывать в треде).
 * base — база модуля; spans — RW-сегменты. Результаты в res. */
void br_probe_all(uintptr_t base, br_probe_t *res);

/* Найти смещение CVehicle* в объекте игрока (через обратную ссылку +0xB8).
 * player_obj — указатель на объект игрока, который находится в машине. */
int  br_find_player_veh_off(uintptr_t player_obj);

/* Найти смещение CPed* в объекте игрока. */
int  br_find_player_ped_off(uintptr_t player_obj, uint16_t veh_id);

#endif
