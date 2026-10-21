/*
 * detect.h — определение сборки и адресов
 *
 * Приоритет:
 *   1) build-id совпал с таблицей → адреса из таблицы, ПЕРЕПРОВЕРЯЕМые пробами;
 *   2) таблица форсирована в настройках → то же;
 *   3) сигнатуры (GetByNetId / пролог диспетчера) → частичные адреса;
 *   4) полный структурный скан (.bss/.data);
 *   5) ручные адреса (вкладка Net).
 */
#ifndef BR_DETECT_H
#define BR_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "off.h"
#include "probe.h"
#include "br_elf.h"

typedef struct {
    uintptr_t base;                 /* база libblackrussia-client.so */
    int       build;                /* br_build (фактическая) */
    const br_build_table_t *tbl;    /* активная таблица */
    br_elf_t  elf;

    /* источники адресов (для UI) */
    char      src_pool[24];
    char      src_mgr[24];
    char      src_ped[24];

    /* ручные переопределения (0 = выкл) */
    uintptr_t custom_pool;
    uintptr_t custom_mgr;
    uintptr_t custom_ped;
    uintptr_t custom_color;
    uintptr_t custom_dispatcher;
    uintptr_t custom_getby;
    uintptr_t custom_create_veh;
    uintptr_t custom_engine_fn;

    br_probe_t probe;
} br_detect_t;

extern br_detect_t g_det;

/* Инициализация (в треде init). 0 = ок. */
int  br_detect_init(void);

/* Повторный поиск (кнопка Rescan). */
void br_detect_rerun(void);

/* Обновить g_st по g_det (после рескана). */
void br_detect_apply(void);

/* ---------- сетевой монитор (packetId глобал, сэмплирование) ---------- */
typedef struct {
    int      running;
    int      last_id;
    double   pps[40];      /* пакетов/сек (окно ~2 с) */
    uint64_t total[40];
    double   started_ms;
} br_monitor_t;

extern br_monitor_t g_mon;

void br_monitor_start(void);
void br_monitor_stop(void);

#endif
