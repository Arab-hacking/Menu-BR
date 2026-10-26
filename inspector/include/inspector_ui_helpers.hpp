#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "imgui.h"
#include "inspector_menu.hpp"

namespace br::ui::detail {

struct ThemePalette {
    ImVec4 accent;
    ImVec4 accent_soft;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 text_muted;
    ImU32  accent_u32;
    ImU32  accent_fill_u32;
    ImU32  card_bg_u32;
    ImU32  card_border_u32;
};

ThemePalette GetThemePalette(InspectorThemeId theme, float bg_alpha) noexcept;

class ScopedAdaptiveTheme {
public:
    ScopedAdaptiveTheme(InspectorThemeId theme, float ui_scale, float bg_alpha);
    ~ScopedAdaptiveTheme();

    ScopedAdaptiveTheme(const ScopedAdaptiveTheme&) = delete;
    ScopedAdaptiveTheme& operator=(const ScopedAdaptiveTheme&) = delete;

private:
    void PushCol(ImGuiCol idx, const ImVec4& col);
    void PushVar(ImGuiStyleVar idx, float val);
    void PushVar(ImGuiStyleVar idx, const ImVec2& val);

    int color_count_ = 0;
    int var_count_   = 0;
};

uint64_t    MonotonicNowMs() noexcept;
bool        ContainsCaseInsensitive(const std::string& haystack, const char* needle) noexcept;
const char* CompassDirectionRu(float yaw_deg) noexcept;

// Векторные иконки разделов 0..6, отрисованные через ImDrawList без внешних текстур
void DrawSectionVectorIcon(ImDrawList* dl, ImVec2 center, float r, int section_id, ImU32 col);

// Цветной бейдж со скруглённым фоном
void DrawStatusPill(const char* text, bool positive, const ThemePalette& pal);

// Наглядная карточка метрики с живым графиком-спарклайном и заливкой под кривой
void DrawMetricSparklineCard(const char* id,
                             const char* title,
                             const char* big_value,
                             const char* subtitle,
                             const float* ring_values,
                             size_t ring_count,
                             size_t ring_cursor,
                             float min_scale,
                             float max_scale,
                             float card_w,
                             float card_h,
                             float ui_scale,
                             const ThemePalette& pal);

// Скруглённая полоса заполнения пула / параметра с подписью
void DrawSmoothProgressBar(const char* label,
                           const char* right_text,
                           float fraction,
                           float height,
                           const ImVec4& fill_color);

// Чип-кнопка быстрого фильтра (переключаемая)
bool DrawFilterChip(const char* label, bool active, const ThemePalette& pal);

} // namespace br::ui::detail
