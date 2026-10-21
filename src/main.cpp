/*
 * main.cpp — точка входа (MenuBR_Init / JNI_OnLoad)
 */
#include "br_common.h"
#include "detect.h"
#include "hooks.h"
#include "input.h"
#include "glapi.h"
#include "ui_menu.h"
#include "ui_gl.h"
#include "jnistub.h"
#include <pthread.h>
#include <fcntl.h>

/* из rt_shim.c: вызов деструкторов статических объектов C++ */
extern "C" void br_cxa_finalize_all(void);

/* forward */
extern "C" int MenuBR_Init(void);

static void *ctor_init_thread(void *arg)
{
    (void)arg;
    MenuBR_Init();
    return 0;
}

/* Самый ранний маркер: конструктор исполняется в момент dlopen,
 * ДО JNI_OnLoad. Если файла brmenu-boot.log нет — dlopen не прошёл
 * (проблема APK: выравнивание/папка/резолв — смотри logcat: grep -i dlopen).
 *
 * СЕЛЬФ-СТАРТ: JNI_OnLoad вызывает JVM только при загрузке через
 * System.loadLibrary; если smali грузит .so обычным dlopen — init
 * запустим сами из конструктора (в отдельном треде, чтобы не держать
 * блокировку dlopen). Повторный вход через JNI_OnLoad безопасен:
 * g_active/g_waiting + mutex. */
__attribute__((constructor)) static void brmenu_ctor(void)
{
    const char *dirs[] = {
        "/sdcard/Android/data/com.br.top/files",
        "/data/data/com.br.top/files",
        "/sdcard/brmenu",
        "/tmp",
    };
    for (size_t i = 0; i < 4; i++) {
        char path[280];
        snprintf(path, sizeof(path), "%s/brmenu-boot.log", dirs[i]);
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) continue;
        char b[128];
        int n = snprintf(b, sizeof(b), "[boot] dlopen OK pid=%d tid=%d time=%ld\n",
                         getpid(), (int)gettid(), (long)time(0));
        if (n > 0) write(fd, b, (size_t)n);
        close(fd);
        break;
    }

    /* старт инициализации (идемпотентен: JNI_OnLoad повторит и вылетит по флагу) */
    pthread_t t;
    if (pthread_create(&t, 0, ctor_init_thread, 0) == 0)
        pthread_detach(t);
}

static volatile int g_active   = 0;
static volatile int g_waiting  = 0;  /* отложенный init идёт */
static volatile int g_shutdown = 0;
static pthread_mutex_t g_init_mtx = PTHREAD_MUTEX_INITIALIZER;

/* GL + хуки + треды + UI. Предусловие: br_detect_init() == BRMENU_OK. */
static int init_after_detect(void)
{
    if (!br_glapi_init()) {
        LOGE("GL API недоступен — выход");
        return BRMENU_ERR_NO_GL;
    }

    int hooked = 0;
    if (br_hook_install(&g_hook_glDrawElements, "glDrawElements", (void *)br_hook_glDrawElements) == 0)
        hooked++;
    if (br_hook_install(&g_hook_glClear, "glClear", (void *)br_hook_glClear) == 0)
        hooked++;
    if (br_hook_install(&g_hook_glDrawArrays, "glDrawArrays", (void *)br_hook_glDrawArrays) == 0)
        hooked++;
    if (hooked == 0) {
        br_plt_hook_t h;
        if (br_hook_install(&h, "glDrawElementsInstanced", (void *)br_hook_glDrawElements) == 0) {
            hooked++;
            LOGW("используется fallback-хук glDrawElementsInstanced");
        }
    }
    if (hooked == 0) {
        LOGE("не удалось поставить ни одного хука");
        return BRMENU_ERR_HOOK;
    }

    br_state_start_sampler();
    br_input_start();
    br_ui_init();

    g_st.local_id = g_cfg.local_id;
    br_feat_set_turbo(g_cfg.turbo_on ? 1 : 0, g_cfg.turbo_mult);
    LOGI("Menu-BR активен (hooks=%d, build=%s)", hooked,
         g_st.build ? br_build_name(g_st.build) : "unknown");
    return BRMENU_OK;
}

/* menu-br может загрузиться ДО libblackrussia-client.so
 * (System.loadLibrary("menu-br") раньше loadLibrary("blackrussia-client")):
 * тогда ждём появления игрового модуля и продолжаем init. */
static void *deferred_init(void *arg)
{
    (void)arg;
    uintptr_t base = 0;
    /* ждём игровую библиотеку до 15 минут: когда именно игра загрузит
     * libblackrussia-client.so зависит от сценария (иногда через
     * минуту-две после старта процесса); статус в лог раз в 60 c */
    for (int i = 0; i < 900; i++) {   /* 900 × 1 c */
        base = br_find_module("libblackrussia-client.so");
        if (base) break;
        if (i > 0 && i % 60 == 0)
            LOGI("deferred: жду libblackrussia-client.so... %d c", i);
        usleep(1000000);
    }

    pthread_mutex_lock(&g_init_mtx);
    int proceed = (base && !g_shutdown && !g_active);
    pthread_mutex_unlock(&g_init_mtx);
    if (!proceed) {
        if (!base) {
            LOGE("игровой модуль не найден за 15 минут — Menu-BR выключен");
            /* диагностика: какие .so реально загружены (имя/путь могли
             * отличаться от ожидаемого) */
            char libs[8192];
            int nl = br_maps_list_libs(libs, sizeof(libs));
            LOGW("libs в процессе: %d шт", nl);
            for (int i = 0; libs[i]; i++)
                if (libs[i] == '\n') libs[i] = ',';
            for (char *q = libs; *q; ) {
                char chunk[180];
                int n = snprintf(chunk, sizeof(chunk), "%.*s", 170, q);
                if (n < 0) n = 0;
                if (n >= (int)sizeof(chunk)) n = (int)sizeof(chunk) - 1;
                chunk[n] = 0;
                LOGW("libs: %s", chunk);
                q += (n > 0 ? n : 1);
            }
        } else {
            LOGI("deferred init: init уже выполнен/остановлен");
        }
        pthread_mutex_lock(&g_init_mtx);
        g_waiting = 0;
        pthread_mutex_unlock(&g_init_mtx);
        return 0;
    }

    LOGI("игровой модуль найден 0x%lx — продолжаю init", (unsigned long)base);
    int rc = br_detect_init();
    if (rc != BRMENU_OK) {
        LOGE("detect: %d", rc);
        rc = BRMENU_ERR_NO_GAME;
    } else {
        rc = init_after_detect();
    }

    pthread_mutex_lock(&g_init_mtx);
    if (rc == BRMENU_OK) g_active = 1;
    g_waiting = 0;
    pthread_mutex_unlock(&g_init_mtx);
    return 0;
}

extern "C" int MenuBR_Init(void)
{
    pthread_mutex_lock(&g_init_mtx);
    if (g_active || g_waiting) {
        pthread_mutex_unlock(&g_init_mtx);
        return BRMENU_OK;
    }

    /* config по умолчанию СНАЧАЛА: br_log_init читает g_cfg.log_level —
     * иначе уровень=0 и ВСЕ строки лога молча отбрасываются */
    br_cfg_init();
    br_log_init("/data/data/com.br.top");
    br_crash_init();                /* дамп на SIGSEGV/SIGABRT/... */
    br_heartbeat_start();
    LOGI("=== Menu-BR init === (build-id цели: 16.110/16.106, soname libblackrussia-client.so)");

    br_cfg_load();                  /* переопределения из файла */
    LOGI("cfg: local_id=%d turbo=%d build_force=%d", g_cfg.local_id, g_cfg.turbo_on, g_cfg.build);

    br_feat_init();

    LOGI("detect: старт");
    int rc = br_detect_init();
    LOGI("detect: rc=%d", rc);
    if (rc == BRMENU_OK) {
        pthread_mutex_unlock(&g_init_mtx);
        rc = init_after_detect();
        pthread_mutex_lock(&g_init_mtx);
        if (rc == BRMENU_OK) g_active = 1;
        pthread_mutex_unlock(&g_init_mtx);
        return rc;
    }

    /* игровой .so ещё не в процессе — отложенный init */
    LOGI("libblackrussia-client.so ещё не загружен — deferred init (до 15 минут)");
    g_waiting = 1;
    pthread_mutex_unlock(&g_init_mtx);

    pthread_t t;
    if (pthread_create(&t, 0, deferred_init, 0) != 0) {
        pthread_mutex_lock(&g_init_mtx);
        g_waiting = 0;
        pthread_mutex_unlock(&g_init_mtx);
        return BRMENU_ERR_NO_GAME;
    }
    pthread_detach(t);
    return BRMENU_OK;
}

extern "C" int MenuBR_Shutdown(void)
{
    pthread_mutex_lock(&g_init_mtx);
    if (!g_active) {
        g_shutdown = 1;
        pthread_mutex_unlock(&g_init_mtx);
        return g_active ? BRMENU_OK : BRMENU_ERR_GENERIC;
    }
    g_shutdown = 1;
    pthread_mutex_unlock(&g_init_mtx);

    br_heartbeat_stop();
    br_state_stop_sampler();
    br_input_stop();
    br_monitor_stop();
    if (g_hook_glDrawElements.installed) br_hook_remove(&g_hook_glDrawElements);
    if (g_hook_glClear.installed) br_hook_remove(&g_hook_glClear);
    if (g_hook_glDrawArrays.installed) br_hook_remove(&g_hook_glDrawArrays);
    br_uigl_shutdown();
    br_feat_shutdown();
    br_cfg_save();
    br_cxa_finalize_all();

    pthread_mutex_lock(&g_init_mtx);
    g_active = 0;
    pthread_mutex_unlock(&g_init_mtx);
    LOGI("Menu-BR остановлен");
    return BRMENU_OK;
}

extern "C" int MenuBR_IsActive(void)
{
    return g_active;
}

/* ---------- JNI ---------- */
extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    ANDROID_LOGI("JNI_OnLoad вызван (vm=%p) — старт MenuBR_Init", (void *)vm);
    (void)reserved;
    MenuBR_Init();
    ANDROID_LOGI("MenuBR_Init возвращён");
    return 0x00010006 /* JNI_VERSION_1_6 */;
}

extern "C" void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm; (void)reserved;
    MenuBR_Shutdown();
}
