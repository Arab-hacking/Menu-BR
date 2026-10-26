/*
 * netmon.h — пассивный Read-Only монитор исходящей сети (16.111)
 *
 * Хуки thunk-методов RakClientInterface:
 *   Send(BitStream*, prio, rel, chan) — raw-пакеты (ID = первый байт)
 *   RPC(id, BitStream*, prio, rel, chan) — RPC
 * BitStream: +0x00 bitsUsed, +0x04 bitsAlloc, +0x08 readOffset, +0x10 data*.
 * Плюс getters RakClient (ping/MTU/connected) через vtable.
 */
#ifndef BR_NETMON_H
#define BR_NETMON_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  kind;      /* 0 = Send, 1 = RPC */
    uint16_t id;
    uint16_t size;      /* байт (bitsUsed/8) */
    uint32_t t;         /* ms от старта монитора */
    uint8_t  head[16];  /* первые байты payload */
} net_event_t;

void br_netmon_init(void);
void br_netmon_stop(void);

/* Установка хуков (target — thunk из таблицы сборки). 0 = ок (хотя бы один). */
int  br_netmon_attach(uintptr_t base, uintptr_t send_va, uintptr_t rpc_va);

/* Точки входа хуков (для br_fhook_install). */
void br_netmon_send_hook(void *bs, int prio, int rel, int chan);
void br_netmon_rpc_hook(int id, void *bs, int prio, int rel, int chan);

/* Снимок: события в newest-first порядке. Возвращает число. */
int  br_netmon_snapshot(net_event_t *out, int max);
uint64_t br_netmon_count(int kind, int id);   /* всего за сессию (0 = нет) */
uint64_t br_netmon_total(int kind);

/* статус (0/1 = недоступно/значение). Только из UI-треда. */
int  br_netmon_ping_avg(void);
int  br_netmon_mtu(void);
int  br_netmon_connected(void);

#endif
