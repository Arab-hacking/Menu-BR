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

/* ---------- алиасы на мangled имена C++ runtime ---------- */
void _Znwm(size_t sz) __attribute__((alias("br_new")));
void _Znam(size_t sz) __attribute__((alias("br_new")));
void _ZdlPv(void *p) __attribute__((alias("br_del")));
void _ZdaPv(void *p) __attribute__((alias("br_del")));
void _ZSt9terminatev(void) __attribute__((alias("br_terminate")));
void __cxa_pure_virtual(void) __attribute__((alias("br_pure_virtual")));
void __cxa_atexit(void (*fn)(void *), void *arg, void *dso_handle)
    __attribute__((alias("br_cxa_atexit")));
