/*
 * feat.h — функции (действия) меню
 *
 * ВАЖНО: все записи в память игры и вызовы функций игры выполняются
 * ТОЛЬКО в GL-треде (внутри хука), чтобы не конфликтовать с тредом игры.
 * UI-тред только кладёт команды в очередь; GL-тред обрабатывает их.
 */
#ifndef BR_FEAT_H
#define BR_FEAT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FEAT_TELEPORT = 0,   /* args: float xyz[3] */
    FEAT_SET_LOCAL,      /* args: int id */
    FEAT_ENGINE,         /* args: int2 { pool_idx, on } */
    FEAT_SPAWN,          /* args: int model_id */
    FEAT_ENTER_SPAWNED,  /* args: - */
    FEAT_SET_COLOR,      /* args: float rgb[3] */
    FEAT_RESCAN,         /* args: - */
    FEAT_COUNT
} br_feat_cmd_t;

void br_feat_init(void);
void br_feat_shutdown(void);

/* Поставить команду (безопасно из любого треда). */
void br_feat_queue(int cmd, const void *args, int args_len);

/* Обработка очереди — только из GL-треда (хук). */
void br_feat_drain(void);

/* Турбо-телепорт (непрерывное ускорение по последнему вектору движения). */
void br_feat_set_turbo(int on, float mult);
int  br_feat_turbo_on(void);
float br_feat_turbo_mult(void);
/* вызывается из GL-треда каждый кадр */
void br_feat_turbo_tick(double dt_ms);

/* Спавн (для UI): последний созданный объект */
uintptr_t br_feat_last_spawned(void);

#endif
