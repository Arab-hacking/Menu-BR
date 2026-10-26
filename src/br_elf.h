/*
 * elf.h — разбор ELF libblackrussia-client.so по памяти процесса
 * (все чтения — через safe_read; бинарник очищен, но .rela.dyn/.rela.plt/.dynsym на месте)
 */
#ifndef BR_ELF_H
#define BR_ELF_H

#include <stdint.h>
#include <stdbool.h>

#define BR_ELF_MAX_SYMS 32768

typedef struct {
    uintptr_t base;          /* базовый адрес модуля в процессе */
    bool      valid;

    /* build-id (SHA1, 20 байт) — из .note.gnu.build-id */
    uint8_t   build_id[20];
    bool      has_build_id;

    /* PLT-информация для хуков */
    uintptr_t plt_start;     /* VA начала .plt (резолвер) */
    int       plt_stub_count;/* число JUMP_SLOT-записей */
    uintptr_t rela_plt_off;  /* VA .rela.plt */
    int       rela_plt_count;
    uintptr_t dynsym_off;
    int       dynsym_count;
    uintptr_t dynstr_off;

    /* кэш символов (имя -> индекс dynsym) */
    struct { char name[64]; int idx; } syms[256];
    int n_syms;
} br_elf_t;

/* Инициализация: разбор по базовому адресу. */
bool br_elf_load(br_elf_t *e, uintptr_t base);

/* Имя по индексу dynsym (буфер out, maxlen). false если индекс неверен. */
bool br_elf_symname(const br_elf_t *e, int idx, char *out, int maxlen);

/* Индекс dynsym по имени, -1 если нет. */
int  br_elf_find_sym(const br_elf_t *e, const char *name);

/* PLT-хук-цель: VA стаба и VA GOT-слота для импортированного символа.
 * idx = индекс в .rela.plt (порядок = порядок стабов после резолвера). */
bool br_elf_plt_for_symbol(const br_elf_t *e, const char *name,
                           uintptr_t *stub_va, uintptr_t *got_va);

/* Чтение строки по VA (буфер). */
bool br_elf_read_cstr(const br_elf_t *e, uintptr_t va, char *out, int maxlen);

/* Бинарный поиск байтового паттерна (pattern + mask; 0x00 в mask = дик) в
 * диапазоне [off, off+len) от базового адреса. Возвращает число совпадений
 * и, если first != 0, смещение первого. */
int  br_elf_scan(const br_elf_t *e, uintptr_t off, uintptr_t len,
                 const uint8_t *pat, const uint8_t *mask, int patlen,
                 uintptr_t *first);

#endif
