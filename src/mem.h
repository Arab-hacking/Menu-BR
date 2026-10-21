/*
 * mem.h — безопасная работа с памятью процесса
 *
 * Все обращения к чужим областям (структуры игры) идут через safe_read/safe_write,
 * которые проверяют, что адрес лежит внутри читаемой(записываемой) области из
 * /proc/self/maps. Никогда не генерируем SIGSEGV в процесс игры.
 */
#ifndef BR_MEM_H
#define BR_MEM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uintptr_t start;
    uintptr_t end;        /* exclusive */
    int       perms;      /* rwx flags */
    char      name[128];  /* basename или [anon] */
} br_range_t;

/* Парсинг /proc/self/maps. Возвращает число областей (<= BR_MAX_RANGES). */
int  br_maps_refresh(void);

/* Адрес лежит в области с нужными правами (need: 1=read, 2=write)? */
bool br_mem_in(uintptr_t addr, uintptr_t len, int need);

/* Безопасные чтения/записи. len <= 256. false = область недоступна. */
bool br_safe_read(uintptr_t addr, void *out, int len);
bool br_safe_write(uintptr_t addr, const void *in, int len);

/* Уровни: 1=u8, 2=u16, 4=u32, 8=u64, 12/16 = vec3/vec4 float */
bool br_readf(uintptr_t addr, float *out, int n);

/* Найти basename .so по строке (например "libblackrussia-client.so").
 * Возвращает базовый адрес первого LOAD-сегмента (vaddr=0) или 0. */
uintptr_t br_find_module(const char *soname);

/* Диапазоны .bss/.data модуля (RW LOAD-сегменты) для сканов. */
typedef struct { uintptr_t start, end; } br_span_t;
int br_module_data_spans(uintptr_t base, br_span_t *out, int max);

#endif
