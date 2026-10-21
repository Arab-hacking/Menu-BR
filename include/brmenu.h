/*
 * Menu-BR — публичный API
 * =======================
 * Инжектируемая библиотека для Black Russia (libblackrussia-client.so, Android arm64).
 *
 * Способ загрузки:
 *   1) dlopen("libmenu-br.so") в процесс игры + вызов MenuBR_Init();
 *   2) либо загрузка из хост-приложения: System.loadLibrary("menu-br")
 *      (тогда JNI_OnLoad запустит инициализацию сам).
 *
 * Библиотека сама:
 *   - находит libblackrussia-client.so в /proc/self/maps,
 *   - определяет сборку (build-id + таблица адресов + структурные пробы),
 *   - ставит PLT-хуки на glClear/glDrawElements/glDrawArrays,
 *   - рисует ImGui-оверлей поверх игры, читает состояние через safe-чтение.
 */
#ifndef BRMENU_H
#define BRMENU_H

#define BR_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

/* Коды возврата */
enum {
    BRMENU_OK              = 0,
    BRMENU_ERR_ALREADY     = -1, /* уже инициализирована */
    BRMENU_ERR_NO_GAME     = -2, /* libblackrussia-client.so не найдена */
    BRMENU_ERR_NO_GL       = -3, /* libGLESv3/libEGL недоступны */
    BRMENU_ERR_HOOK        = -4, /* не удалось поставить хуки */
    BRMENU_ERR_GENERIC     = -99,
};

/* Инициализация (идемпотентна). Вызывать из любого треда процесса игры. */
BR_EXPORT int  MenuBR_Init(void);

/* Остановка: снятие хуков, завершение тредов, освобождение. */
BR_EXPORT int  MenuBR_Shutdown(void);

/* 1 = активна */
BR_EXPORT int  MenuBR_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* BRMENU_H */
