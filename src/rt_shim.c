/*
 * rt_shim.c — самодостаточный C++ runtime для инжектовой библиотеки
 *
 * Библиотека компилируется как C++ (ImGui), но НЕ линкуется ни с libstdc++,
 * ни с libc++ — все runtime-символы предоставляются здесь (алиасы на простые
 * реализации поверх malloc/free/abort). В результате .so импортирует только
 * стандартные POSIX-символы, которые есть в bionic (libc.so) любого Android:
 *   malloc, free, abort, pthread_*, dlopen, dlsym, open, read, write,
 *   mmap, mprotect, snprintf и т.п.
 *
 * Компиляция ядра: -fno-exceptions -fno-rtti.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void br_die(const char *what)
{
    /* fprintf может быть недоступен в экстренном случае — но в bionic есть */
    fprintf(stderr, "[MenuBR] fatal: %s\n", what);
    abort();
}

void *br_new(size_t sz)
{
    void *p = malloc(sz ? sz : 1);
    if (!p) br_die("operator new: OOM");
    return p;
}

void br_del(void *p) { free(p); }

void br_terminate(void) { br_die("std::terminate"); }

void br_pure_virtual(void) { br_die("pure virtual call"); }

/* __cxa_atexit: регистрируем деструкторы статических объектов (небольшая таблица) */
#define BR_AT_MAX 128
static void (*br_at_fns[BR_AT_MAX])(void *);
static void *br_at_args[BR_AT_MAX];
static int br_at_n = 0;

void br_cxa_atexit(void (*fn)(void *), void *arg, void *dso_handle)
{
    (void)dso_handle;
    if (br_at_n < BR_AT_MAX) {
        br_at_fns[br_at_n] = fn;
        br_at_args[br_at_n] = arg;
        br_at_n++;
    }
}

/* вызов регистрированных деструкторов (вызывается при MenuBR_Shutdown) */
void br_cxa_finalize_all(void)
{
    while (br_at_n > 0) {
        br_at_n--;
        if (br_at_fns[br_at_n]) br_at_fns[br_at_n](br_at_args[br_at_n]);
    }
}

/* ---------------------------------------------------------------------------
 * ctype-таблицы glibc-хедеров zig (build-compat): в хедерах zig isprint/c
 * реализованы макросом (*__ctype_b_loc())[(int)c], а tolower/toupper —
 * через __ctype_tolower_loc/__ctype_toupper_loc. bionic таких символов не
 * экспортирует, поэтому даём корректную C-locale таблицу (384 ячейки:
 * -128..255, индекс = c + 128).
 *
 * Биты (little-endian encoding glibc _ISbit):
 *   upper=0x0100 lower=0x0200 alpha=0x0400 digit=0x0800 xdigit=0x1000
 *   space=0x2000 print=0x4000 graph=0x8000 blank=0x01 cntrl=0x02
 *   punct=0x04 alnum=0x08
 * ------------------------------------------------------------------------- */
static unsigned short br_ctype_b_table[384];
static const unsigned short *br_ctype_b_loc_val = br_ctype_b_table;
static int32_t br_ctype_tolower_table[384];
static int32_t br_ctype_toupper_table[384];
static const int32_t *br_ctype_tolower_loc_val = br_ctype_tolower_table;
static const int32_t *br_ctype_toupper_loc_val = br_ctype_toupper_table;
static volatile int br_ctype_ready = 0;

static void br_ctype_fill(void)
{
    for (int i = 0; i < 384; ++i) {
        int c = i - 128;
        unsigned short f = 0;
        if (c < 0) {
            f = 0; /* отрицательные: пусто (EOF=-1 обрабатывает макрос сам) */
        } else if ((c < 32) || (c == 127)) {
            f = 0x02; /* cntrl: 0..8, 14..31, 127 (9..13 = space) */
            if (c >= 9 && c <= 13) f = 0x2000; /* nl vt ff cr — whitespace */
            if (c == 9) f |= 0x01;             /* tab — blank */
        } else {
            f = 0x4000; /* print: 32..126 */
            if (c == 32) {
                f |= 0x2000 | 0x01; /* space + blank */
            } else if (c >= 'a' && c <= 'z') {
                f |= 0x0200 | 0x0400 | 0x08; /* lower alpha alnum */
                if (c <= 'f') f |= 0x1000;   /* xdigit */
            } else if (c >= 'A' && c <= 'Z') {
                f |= 0x0100 | 0x0400 | 0x08; /* upper alpha alnum */
                if (c <= 'F') f |= 0x1000;   /* xdigit */
            } else if (c >= '0' && c <= '9') {
                f |= 0x0800 | 0x1000 | 0x08; /* digit xdigit alnum */
            } else {
                f |= 0x04; /* punct */
            }
        }
        br_ctype_b_table[i] = f;

        br_ctype_tolower_table[i] = c;
        br_ctype_toupper_table[i] = c;
        if (c >= 'A' && c <= 'Z') br_ctype_tolower_table[i] = c + 32;
        if (c >= 'a' && c <= 'z') br_ctype_toupper_table[i] = c - 32;
    }
}

const unsigned short int **__ctype_b_loc(void)
{
    if (!br_ctype_ready) { br_ctype_fill(); br_ctype_ready = 1; }
    return &br_ctype_b_loc_val;
}

const int32_t **__ctype_tolower_loc(void)
{
    if (!br_ctype_ready) { br_ctype_fill(); br_ctype_ready = 1; }
    return &br_ctype_tolower_loc_val;
}

const int32_t **__ctype_toupper_loc(void)
{
    if (!br_ctype_ready) { br_ctype_fill(); br_ctype_ready = 1; }
    return &br_ctype_toupper_loc_val;
}

/* ---------- алиасы на мangled имена C++ runtime ---------- */
void _Znwm(size_t sz) __attribute__((alias("br_new")));
void _Znam(size_t sz) __attribute__((alias("br_new")));
void _ZdlPv(void *p) __attribute__((alias("br_del")));
void _ZdaPv(void *p) __attribute__((alias("br_del")));
void _ZSt9terminatev(void) __attribute__((alias("br_terminate")));
void __cxa_pure_virtual(void) __attribute__((alias("br_pure_virtual")));
void __cxa_atexit(void (*fn)(void *), void *arg, void *dso_handle)
    __attribute__((alias("br_cxa_atexit")));
