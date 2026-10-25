#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "imgui.h"
#include "br_offsets_16_111_19050.hpp"

namespace br::ui {

// Диагностическая таблица модуля и состояния собственного (Standalone) рендера Dear ImGui 1.90.9
struct InternalImGuiRuntimeTable {
    uintptr_t module_base                = 0;
    bool      build_id_verified          = false;
    uint8_t   observed_build_id[20]      = {};

    // Состояние собственного изолированного контекста ImGui (Standalone GLES 3.0)
    bool          standalone_mode        = true;
    ImGuiContext* standalone_ctx         = nullptr;
    const char*   render_hook_name       = "Не установлен";
    const char*   touch_hook_name        = "Не установлен";
    const char*   loaded_font_label      = "Default ASCII";
    int           surface_width          = 0;
    int           surface_height         = 0;
    float         last_touch_x           = 0.0f;
    float         last_touch_y           = 0.0f;
    bool          last_touch_down        = false;
    uint64_t      touch_events_total     = 0;
    uint64_t      rendered_frames_total  = 0;

    // Справочные Read-Only указатели на внутренние структуры клиента (только для диагностики)
    ImGuiContext**    p_gimgui           = nullptr; // base + 0x03C9D9D0
    ImGuiMemAllocFunc alloc_fn           = nullptr; // *(base + 0x0229C350) -> 0x019D73AC
    ImGuiMemFreeFunc  free_fn            = nullptr; // *(base + 0x0229C358) -> 0x019D73B0
    void**            p_alloc_user_data  = nullptr; // base + 0x03C9D9E0
    float*            p_font_scale       = nullptr; // base + 0x0363D528
    ImFont**          p_loaded_fonts     = nullptr; // base + 0x0363D530 (6 слотов)
    uint8_t*          p_frame_active     = nullptr; // base + 0x0363D560
};

class InternalImGuiBridge {
public:
    using FrameRenderCallback = std::function<void(const InternalImGuiRuntimeTable&)>;

    // Поиск базы загруженной libblackrussia-client.so через dl_iterate_phdr / /proc/self/maps
    static uintptr_t FindModuleBase(const char* module_name = offsets::kTargetModuleName) noexcept;

    // Заполнение диагностических адресов и проверка 20-байтового ELF GNU Build-ID (16.111.19050)
    static bool ResolveSymbols(uintptr_t module_base,
                               InternalImGuiRuntimeTable& out_table,
                               bool require_exact_build_id = true) noexcept;

    // Запуск полностью собственного (Standalone) контекста Dear ImGui 1.90.9 поверх OpenGL ES 3.0
    // Перехватывает кадр рендера (JNILib_step / eglSwapBuffers) и мультитач (JNILib_multiTouchEvent)
    static bool AttachStandaloneRenderer(uintptr_t module_base,
                                         FrameRenderCallback on_render_frame);

    // Совместимый алиас для вызова из старого кода
    static bool AttachToInternalRenderer(uintptr_t module_base,
                                         FrameRenderCallback on_render_frame,
                                         bool /*require_exact_build_id*/ = false) {
        return AttachStandaloneRenderer(module_base, std::move(on_render_frame));
    }

    // Потокобезопасная подача события касания извне (0=DOWN, 1=UP, 2=MOVE, 3=CANCEL)
    static bool FeedTouchEvent(int action, float x, float y) noexcept;

    static void Detach() noexcept;

    static const InternalImGuiRuntimeTable& GetRuntimeTable() noexcept;
    static bool IsAttached() noexcept;
    static std::string FormatBuildIdHex(const uint8_t build_id[20]);
};

} // namespace br::ui
