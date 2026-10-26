/*
 * br_common.h — базовые типы, атомарные помощники, объявление глобального состояния
 */
#ifndef BR_COMMON_H
#define BR_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "brmenu.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define ANDROID_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "BRMenu", __VA_ARGS__)
#define ANDROID_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "BRMenu", __VA_ARGS__)
#define ANDROID_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BRMenu", __VA_ARGS__)
#else
#define ANDROID_LOGI(...) (void)0
#define ANDROID_LOGW(...) (void)0
#define ANDROID_LOGE(...) (void)0
#endif

#define BR_MAX_PLAYERS   1504   /* GetPlayerById: id <= 1503 (0x5DF) */
#define BR_MAX_VEHICLES  2000   /* VehiclePool: 2000 слотов (id <= 1999 = 0x7CF) */
#define BR_PED_STRIDE    0xF8   /* таблица педов: 0xF8 байт на слот */
#define BR_COLOR_STRIDE  16     /* таблица цветов: 16 байт на игрока */

/* Игровые классы машин (CVehicle+0x430 / modelInfo+0x5c) */
static const char *br_vehicle_class_name(int cls)
{
    switch (cls) {
    case 0:  return "car";
    case 1:  return "boat";
    case 2:  return "train";
    case 3:  return "heli";
    case 4:  return "plane";
    case 5:  return "bike";
    case 6:  return "mtruck";
    case 7:  return "trailer";
    case 8:  return "quad";
    case 9:  return "bmx";
    case 10: return "trike";
    case 11: return "portcrane";
    case 12: return "crane";
    default: return "?";
    }
}

/* ID синхро-пакетов (канал не-RPC) */
#define BR_SYNC_PLAYER   30
#define BR_SYNC_VEHICLE  32
#define BR_SYNC_LINK     33
#define BR_SYNC_INCAR    34
#define BR_SYNC_ONFOOT   38
#define BR_SYNC_CRANE    39
#define BR_SYNC_AUX      29
#define BR_SYNC_SRV1     14
#define BR_SYNC_SRV2     19
#define BR_RPC_CHANNEL   11

static inline double br_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static inline double br_wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---------- forward decls всех модулей (единая точка включения) ---------- */
void  br_log_init(const char *dir);
void  br_log(const char *lvl, const char *fmt, ...);
int   br_log_read(char *out, int out_len, int lines, char *lvl_out);
void  br_crash_init(void);        /* SIGSEGV/... -> дамп (bt + maps) в лог */
void  br_heartbeat_start(void);   /* "alive" каждые 2 c в logcat */
void  br_heartbeat_stop(void);
#define LOGI(...) br_log("I", __VA_ARGS__)
#define LOGW(...) br_log("W", __VA_ARGS__)
#define LOGE(...) br_log("E", __VA_ARGS__)

#include "mem.h"
#include "br_elf.h"
#include "off.h"
#include "probe.h"
#include "cfg.h"
#include "state.h"
#include "feat.h"

#endif /* BR_COMMON_H */
