/*
 * func_hook.h — хук ФУНКЦИЙ ARM64 (не PLT) с трэмплином и релокацией
 *
 * Порт проверенной на устройстве реализации (br_imgui_inspector 16.111):
 *   - копирует первые 4 инструкции (16 B),
 *   - ADRP/ADR/B/BL в прологе релоцируются в MOVZ/MOVK (64-битная загрузка),
 *   - пролог с PC-зависимыми условными переходами (B.cond/CBZ/TBZ/LDR literal)
 *     отклоняется (16-байтный перехват небезопасен),
 *   - патч цели: `ldr x17, #8; br x17; <имм64>` (абсолютный, без радиуса ±128МБ),
 *   - trampoline возвращает в orig+16,
 *   - кэш инструкций сбрасывается (__clear_cache).
 *
 * Работает только на aarch64 (host-сборки: stub no-op).
 */
#ifndef BR_FUNC_HOOK_H
#define BR_FUNC_HOOK_H

#include <stdint.h>
#include <stdbool.h>

#define BR_FHOOK_MAX 8

typedef struct {
    uintptr_t target;
    uintptr_t detour;
    uintptr_t trampoline;   /* 0 = не установлен */
    size_t    tramp_size;   /* размер mmap (для munmap) */
    uint32_t  saved[4];     /* оригинальные 4 инструкции (для снятия) */
    int       active;
} br_fhook_t;

/* Установка: target — адрес функции (runtime), detour — наша обёртка.
 * out_orig получает адрес трэмплина (вызов оригинала: через него же аргументы
 * передаются как есть — сигнатура детура должна совпадать с оригиналом).
 * 0 = успех. */
int  br_fhook_install(void *target, void *detour, void **out_orig);

/* Снять все хуки (восстановить оригинальные инструкции). */
void br_fhook_remove_all(void);

/* Список установленных (для UI/логов). */
const br_fhook_t *br_fhook_list(int *count);

#endif
