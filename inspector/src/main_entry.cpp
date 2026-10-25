#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#include <jni.h>
#define BR_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "BRInspector16111", fmt, ##__VA_ARGS__)
#define BR_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "BRInspector16111", fmt, ##__VA_ARGS__)
#define BR_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "BRInspector16111", fmt, ##__VA_ARGS__)
#else
#define BR_LOGI(fmt, ...) std::fprintf(stdout, "[BRInspector16111][INFO] " fmt "\n", ##__VA_ARGS__)
#define BR_LOGW(fmt, ...) std::fprintf(stdout, "[BRInspector16111][WARN] " fmt "\n", ##__VA_ARGS__)
#define BR_LOGE(fmt, ...) std::fprintf(stderr, "[BRInspector16111][ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

#include "arm64_hook.hpp"
#include "br_offsets_16_111_19050.hpp"
#include "inspector_menu.hpp"
#include "internal_imgui_bridge.hpp"

namespace {

std::atomic<bool>     g_worker_started{false};
std::atomic<bool>     g_stop_requested{false};
br::ui::InspectorMenu g_inspector_menu;

void BackgroundAttachWorker() {
    BR_LOGI("Запуск автономного Dear ImGui 1.90.9 (Standalone OpenGL ES 3.0) для %s (%s)...",
            br::offsets::kTargetModuleName,
            br::offsets::kTargetBuildVersion);

    uintptr_t base = 0;
    for (int attempt = 0; attempt < 150 && !g_stop_requested.load(); ++attempt) {
        base = br::ui::InternalImGuiBridge::FindModuleBase(br::offsets::kTargetModuleName);
        if (base != 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_stop_requested.load()) {
        return;
    }

    if (base != 0) {
        br::ui::InternalImGuiRuntimeTable probe{};
        br::ui::InternalImGuiBridge::ResolveSymbols(base, probe, /*require_exact_build_id=*/false);
        const std::string seen = br::ui::InternalImGuiBridge::FormatBuildIdHex(probe.observed_build_id);
        if (probe.build_id_verified) {
            BR_LOGI("Модуль %s найден @ 0x%lx, Build-ID совпадает (%s).",
                    br::offsets::kTargetModuleName,
                    static_cast<unsigned long>(base),
                    seen.c_str());
        } else {
            BR_LOGW("Модуль %s найден @ 0x%lx, Build-ID отличается (%s), запускаем собственный ImGui в диагностическом режиме.",
                    br::offsets::kTargetModuleName,
                    static_cast<unsigned long>(base),
                    seen.c_str());
        }
    } else {
        BR_LOGW("Модуль %s пока не найден, пробуем подключить собственный ImGui через eglSwapBuffers...",
                br::offsets::kTargetModuleName);
    }

    // Не ждём клиентский GImGui — сразу подключаем собственный рендер ImGui_ImplOpenGL3
    const bool ok = br::ui::InternalImGuiBridge::AttachStandaloneRenderer(
        base,
        [](const br::ui::InternalImGuiRuntimeTable& rt) {
            g_inspector_menu.Render(rt);
        });

    if (ok) {
        const auto& rt = br::ui::InternalImGuiBridge::GetRuntimeTable();
        BR_LOGI("Собственный ImGui подключён! RenderHook='%s', TouchHook='%s', Base=0x%lx",
                rt.render_hook_name ? rt.render_hook_name : "-",
                rt.touch_hook_name ? rt.touch_hook_name : "-",
                static_cast<unsigned long>(base));
    } else {
        BR_LOGE("Не удалось установить перехват кадра OpenGL ES 3.0.");
    }
}

void StartInspectorOnce() {
    bool expected = false;
    if (g_worker_started.compare_exchange_strong(expected, true)) {
        std::thread(BackgroundAttachWorker).detach();
    }
}

} // namespace

// Публичный C API для ручной интеграции с внешним загрузчиком или Dobby/ShadowHook
extern "C" __attribute__((visibility("default")))
void br_inspector_set_hook_backend(br::hook::ExternalHookBackendFn fn) {
    br::hook::Arm64Hook::SetExternalBackend(fn);
}

extern "C" __attribute__((visibility("default")))
void br_inspector_init() {
    StartInspectorOnce();
}

extern "C" __attribute__((visibility("default")))
void br_inspector_shutdown() {
    g_stop_requested.store(true);
    br::ui::InternalImGuiBridge::Detach();
}

extern "C" __attribute__((visibility("default")))
bool br_inspector_feed_touch(int action, float x, float y) {
    return br::ui::InternalImGuiBridge::FeedTouchEvent(action, x, y);
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* /*vm*/, void* /*reserved*/) {
    BR_LOGI("JNI_OnLoad вызван -> инициализация собственного ImGui меню.");
    StartInspectorOnce();
    return JNI_VERSION_1_6;
}

__attribute__((constructor))
static void OnLibraryLoadedConstructor() {
    StartInspectorOnce();
}

__attribute__((destructor))
static void OnLibraryUnloadedDestructor() {
    br_inspector_shutdown();
}
#endif
