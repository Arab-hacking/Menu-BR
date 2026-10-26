/*
 * hooks.h — PLT-хуки (ARM64)
 *
 * Механика:
 *  1) в .so игры находим PLT-стаб для импортированного символа (.rela.plt),
 *  2) резервируем RWX-страницу-трэмплин БЛИЗКО к стабу (mmap с подсказкой),
 *     копируем туда оригинальные 16 байт стаба (это и есть вызов оригинала),
 *  3) последнюю инструкцию стаба (br x17) перезаписываем на `b trampoline`.
 *     Гонка безопасна: тред либо уйдёт в оригинал (старый br), либо в наш
 *     хук (новый br) — оба пути корректны.
 */
#ifndef BR_HOOKS_H
#define BR_HOOKS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char      sym[64];
    uintptr_t stub;        /* абсолютный адрес стаба в процессе */
    uintptr_t got;
    uint32_t  orig[4];     /* оригинальные 16 байт */
    uintptr_t tramp;       /* RWX-трэмплин (16 байт = оригинал) */
    void     *hook_fn;
    bool      installed;
} br_plt_hook_t;

/* Установка. hook_fn — функция с той же сигнатурой, что и сим. */
int  br_hook_install(br_plt_hook_t *h, const char *sym, void *hook_fn);

/* Снятие (восстановление). */
void br_hook_remove(br_plt_hook_t *h);

/* Трэмплин = копия оригинального стаба; вызываем напрямую со своей сигнатурой:
 *   ((void (*)(int,int,int,const void*))h->tramp)(mode, count, type, idx); */

/* Счётчики кадров (для якоря отрисовки UI) */
extern volatile int   g_draws_since_clear;
extern volatile int   g_frame_ui_on; /* 1 = сейчас рисуем UI (анти-рекурсия) */

/* Глобальные хуки (объявлены в hooks.cpp, используются в main.cpp) */
extern br_plt_hook_t g_hook_glDrawElements;
extern br_plt_hook_t g_hook_glClear;
extern br_plt_hook_t g_hook_glDrawArrays;

/* Обработчики (сигнатуры GL) */
void br_hook_glDrawElements(int mode, int count, int type, const void *indices);
void br_hook_glClear(int mask);
void br_hook_glDrawArrays(int mode, int first, int count);

#endif
