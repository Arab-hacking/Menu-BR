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

/* из rt_shim.c: вызов деструкторов статических объектов C++ */
extern "C" void br_cxa_finalize_all(void);

static volatile int g_active = 0;

/* JNI (если библиотека загружена System.loadLibrary из хост-приложения) */
static JavaVM *g_jvm = 0;

extern "C" int MenuBR_Init(void)
{
    if (g_active) return BRMENU_OK;

    br_cfg_init();
    br_cfg_load();
    br_log_init("/sdcard/brmenu");
    LOGI("=== Menu-BR init ===");

    br_feat_init();

    /* 1) определение сборки и адресов */
    int rc = br_detect_init();
    if (rc != BRMENU_OK) {
        LOGE("detect: %d — хуки не ставим, но пробуем продолжить (Net/ручные)", rc);
    }

    /* 2) GL/EGL/Android API */
    if (!br_glapi_init()) {
        LOGE("GL API недоступен — выход");
        return BRMENU_ERR_NO_GL;
    }

    /* 3) PLT-хуки (GL-якоря) */
    int hooked = 0;
    if (br_hook_install(&g_hook_glDrawElements, "glDrawElements", (void *)br_hook_glDrawElements) == 0)
        hooked++;
    if (br_hook_install(&g_hook_glClear, "glClear", (void *)br_hook_glClear) == 0)
        hooked++;
    if (br_hook_install(&g_hook_glDrawArrays, "glDrawArrays", (void *)br_hook_glDrawArrays) == 0)
        hooked++;
    if (hooked == 0) {
        /* fallback: instanced draw тоже идёт через PLT */
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

    /* 4) треды */
    br_state_start_sampler();
    br_input_start();

    /* 5) UI-контекст (без GL — GL подхватится в первом кадре) */
    br_ui_init();

    g_active = 1;
    g_st.local_id = g_cfg.local_id;
    br_feat_set_turbo(g_cfg.turbo_on ? 1 : 0, g_cfg.turbo_mult);
    LOGI("Menu-BR активен (hooks=%d, build=%s)", hooked,
         g_st.build ? br_build_name(g_st.build) : "unknown");
    return BRMENU_OK;
}

extern "C" int MenuBR_Shutdown(void)
{
    if (!g_active) return BRMENU_ERR_GENERIC;
    br_input_stop();
    br_monitor_stop();
    if (g_hook_glDrawElements.installed) br_hook_remove(&g_hook_glDrawElements);
    if (g_hook_glClear.installed) br_hook_remove(&g_hook_glClear);
    if (g_hook_glDrawArrays.installed) br_hook_remove(&g_hook_glDrawArrays);
    br_uigl_shutdown();
    br_feat_shutdown();
    br_cfg_save();
    br_cxa_finalize_all();
    g_active = 0;
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
    (void)reserved;
    g_jvm = vm;
    MenuBR_Init();
    return 0x00010006 /* JNI_VERSION_1_6 */;
}

extern "C" void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm; (void)reserved;
    MenuBR_Shutdown();
}
