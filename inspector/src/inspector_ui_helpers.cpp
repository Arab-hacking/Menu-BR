#include "inspector_ui_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "imgui_internal.h"

namespace br::ui::detail {

uint64_t MonotonicNowMs() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) noexcept {
    if (!needle || needle[0] == '\0') {
        return true;
    }
    const size_t nlen = std::strlen(needle);
    if (nlen > haystack.size()) {
        return false;
    }
    for (size_t i = 0; i + nlen <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            unsigned char a = static_cast<unsigned char>(haystack[i + j]);
            unsigned char b = static_cast<unsigned char>(needle[j]);
            if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

const char* CompassDirectionRu(float yaw_deg) noexcept {
    float a = std::fmod(yaw_deg, 360.0f);
    if (a < 0.0f) {
        a += 360.0f;
    }
    static constexpr const char* kDirs[8] = {
        "С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"
    };
    const int idx = static_cast<int>(std::floor((a + 22.5f) / 45.0f)) & 7;
    return kDirs[idx];
}

ThemePalette GetThemePalette(InspectorThemeId theme, float bg_alpha) noexcept {
    const float a = std::clamp(bg_alpha, 0.55f, 1.00f);
    ThemePalette p{};
    switch (theme) {
        case InspectorThemeId::CrimsonCarbon:
            p.accent      = ImVec4(0.96f, 0.26f, 0.34f, 1.0f);
            p.accent_soft = ImVec4(0.96f, 0.26f, 0.34f, 0.22f);
            p.success     = ImVec4(0.24f, 0.86f, 0.54f, 1.0f);
            p.warning     = ImVec4(0.98f, 0.75f, 0.24f, 1.0f);
            p.danger      = ImVec4(0.98f, 0.32f, 0.32f, 1.0f);
            p.text_muted  = ImVec4(0.64f, 0.66f, 0.72f, 1.0f);
            p.card_bg_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.11f, 0.10f, 0.12f, a * 0.94f));
            p.card_border_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.36f, 0.18f, 0.22f, 0.75f));
            break;

        case InspectorThemeId::EmeraldMatrix:
            p.accent      = ImVec4(0.18f, 0.90f, 0.62f, 1.0f);
            p.accent_soft = ImVec4(0.18f, 0.90f, 0.62f, 0.20f);
            p.success     = ImVec4(0.26f, 0.94f, 0.62f, 1.0f);
            p.warning     = ImVec4(0.96f, 0.80f, 0.26f, 1.0f);
            p.danger      = ImVec4(0.95f, 0.36f, 0.38f, 1.0f);
            p.text_muted  = ImVec4(0.56f, 0.70f, 0.64f, 1.0f);
            p.card_bg_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.11f, 0.09f, a * 0.94f));
            p.card_border_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.16f, 0.38f, 0.28f, 0.75f));
            break;

        case InspectorThemeId::DaylightFrost:
            p.accent      = ImVec4(0.08f, 0.46f, 0.86f, 1.0f);
            p.accent_soft = ImVec4(0.08f, 0.46f, 0.86f, 0.16f);
            p.success     = ImVec4(0.08f, 0.62f, 0.34f, 1.0f);
            p.warning     = ImVec4(0.82f, 0.52f, 0.04f, 1.0f);
            p.danger      = ImVec4(0.82f, 0.18f, 0.22f, 1.0f);
            p.text_muted  = ImVec4(0.36f, 0.42f, 0.50f, 1.0f);
            p.card_bg_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.96f, 0.97f, 0.99f, a * 0.96f));
            p.card_border_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.74f, 0.80f, 0.88f, 0.90f));
            break;

        case InspectorThemeId::CyberObsidian:
        default:
            p.accent      = ImVec4(0.20f, 0.80f, 0.98f, 1.0f);
            p.accent_soft = ImVec4(0.20f, 0.80f, 0.98f, 0.20f);
            p.success     = ImVec4(0.22f, 0.88f, 0.54f, 1.0f);
            p.warning     = ImVec4(0.98f, 0.76f, 0.24f, 1.0f);
            p.danger      = ImVec4(0.96f, 0.36f, 0.38f, 1.0f);
            p.text_muted  = ImVec4(0.58f, 0.66f, 0.76f, 1.0f);
            p.card_bg_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.08f, 0.11f, 0.16f, a * 0.95f));
            p.card_border_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.30f, 0.44f, 0.78f));
            break;
    }
    p.accent_u32      = ImGui::ColorConvertFloat4ToU32(p.accent);
    p.accent_fill_u32 = ImGui::ColorConvertFloat4ToU32(p.accent_soft);
    return p;
}

ScopedAdaptiveTheme::ScopedAdaptiveTheme(InspectorThemeId theme, float ui_scale, float bg_alpha) {
    const float s = std::clamp(ui_scale, 0.70f, 2.50f);
    const float a = std::clamp(bg_alpha, 0.55f, 1.00f);

    PushVar(ImGuiStyleVar_WindowRounding,    13.0f * s);
    PushVar(ImGuiStyleVar_ChildRounding,     10.0f * s);
    PushVar(ImGuiStyleVar_FrameRounding,      7.0f * s);
    PushVar(ImGuiStyleVar_PopupRounding,     10.0f * s);
    PushVar(ImGuiStyleVar_ScrollbarRounding, 10.0f * s);
    PushVar(ImGuiStyleVar_GrabRounding,       7.0f * s);
    PushVar(ImGuiStyleVar_TabRounding,        7.0f * s);
    PushVar(ImGuiStyleVar_WindowBorderSize,   1.0f);
    PushVar(ImGuiStyleVar_ChildBorderSize,    1.0f);
    PushVar(ImGuiStyleVar_WindowPadding,     ImVec2(12.0f * s, 11.0f * s));
    PushVar(ImGuiStyleVar_FramePadding,      ImVec2(11.0f * s, 8.0f * s));
    PushVar(ImGuiStyleVar_ItemSpacing,       ImVec2(9.0f * s, 8.0f * s));
    PushVar(ImGuiStyleVar_CellPadding,       ImVec2(8.0f * s, 6.5f * s));
    PushVar(ImGuiStyleVar_ScrollbarSize,     20.0f * s);

    if (theme == InspectorThemeId::DaylightFrost) {
        PushCol(ImGuiCol_Text,                 ImVec4(0.10f, 0.13f, 0.18f, 1.00f));
        PushCol(ImGuiCol_TextDisabled,         ImVec4(0.42f, 0.47f, 0.55f, 1.00f));
        PushCol(ImGuiCol_WindowBg,             ImVec4(0.92f, 0.94f, 0.97f, a));
        PushCol(ImGuiCol_ChildBg,              ImVec4(0.96f, 0.97f, 0.99f, a * 0.92f));
        PushCol(ImGuiCol_PopupBg,              ImVec4(0.98f, 0.99f, 1.00f, 0.98f));
        PushCol(ImGuiCol_Border,               ImVec4(0.75f, 0.80f, 0.88f, 0.90f));
        PushCol(ImGuiCol_FrameBg,              ImVec4(0.85f, 0.89f, 0.94f, 1.00f));
        PushCol(ImGuiCol_FrameBgHovered,       ImVec4(0.78f, 0.85f, 0.94f, 1.00f));
        PushCol(ImGuiCol_FrameBgActive,        ImVec4(0.70f, 0.80f, 0.94f, 1.00f));
        PushCol(ImGuiCol_TitleBg,              ImVec4(0.84f, 0.88f, 0.94f, 1.00f));
        PushCol(ImGuiCol_TitleBgActive,        ImVec4(0.78f, 0.85f, 0.95f, 1.00f));
        PushCol(ImGuiCol_CheckMark,            ImVec4(0.08f, 0.46f, 0.86f, 1.00f));
        PushCol(ImGuiCol_SliderGrab,           ImVec4(0.14f, 0.52f, 0.90f, 1.00f));
        PushCol(ImGuiCol_SliderGrabActive,     ImVec4(0.06f, 0.38f, 0.78f, 1.00f));
        PushCol(ImGuiCol_Button,               ImVec4(0.82f, 0.87f, 0.94f, 1.00f));
        PushCol(ImGuiCol_ButtonHovered,        ImVec4(0.72f, 0.82f, 0.95f, 1.00f));
        PushCol(ImGuiCol_ButtonActive,         ImVec4(0.60f, 0.75f, 0.95f, 1.00f));
        PushCol(ImGuiCol_Header,               ImVec4(0.78f, 0.86f, 0.96f, 0.85f));
        PushCol(ImGuiCol_HeaderHovered,        ImVec4(0.70f, 0.82f, 0.96f, 0.95f));
        PushCol(ImGuiCol_HeaderActive,         ImVec4(0.62f, 0.77f, 0.96f, 1.00f));
        PushCol(ImGuiCol_Tab,                  ImVec4(0.84f, 0.88f, 0.94f, 1.00f));
        PushCol(ImGuiCol_TabHovered,           ImVec4(0.70f, 0.82f, 0.96f, 1.00f));
        PushCol(ImGuiCol_TabSelected,          ImVec4(0.12f, 0.50f, 0.88f, 1.00f));
        PushCol(ImGuiCol_TableHeaderBg,        ImVec4(0.84f, 0.88f, 0.94f, 1.00f));
        PushCol(ImGuiCol_TableRowBg,           ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        PushCol(ImGuiCol_TableRowBgAlt,        ImVec4(0.05f, 0.15f, 0.30f, 0.04f));
        return;
    }

    const ThemePalette pal = GetThemePalette(theme, a);
    ImVec4 win_bg   = ImVec4(0.05f, 0.07f, 0.10f, a);
    ImVec4 child_bg = ImVec4(0.08f, 0.10f, 0.15f, a * 0.92f);
    ImVec4 frame_bg = ImVec4(0.11f, 0.15f, 0.21f, 1.00f);
    ImVec4 border_c = ImVec4(0.19f, 0.27f, 0.38f, 0.82f);

    if (theme == InspectorThemeId::CrimsonCarbon) {
        win_bg   = ImVec4(0.07f, 0.06f, 0.08f, a);
        child_bg = ImVec4(0.10f, 0.09f, 0.11f, a * 0.92f);
        frame_bg = ImVec4(0.16f, 0.12f, 0.15f, 1.00f);
        border_c = ImVec4(0.36f, 0.18f, 0.22f, 0.82f);
    } else if (theme == InspectorThemeId::EmeraldMatrix) {
        win_bg   = ImVec4(0.04f, 0.08f, 0.06f, a);
        child_bg = ImVec4(0.06f, 0.11f, 0.09f, a * 0.92f);
        frame_bg = ImVec4(0.09f, 0.17f, 0.13f, 1.00f);
        border_c = ImVec4(0.16f, 0.36f, 0.27f, 0.82f);
    }

    PushCol(ImGuiCol_Text,                 ImVec4(0.94f, 0.96f, 0.99f, 1.00f));
    PushCol(ImGuiCol_TextDisabled,         pal.text_muted);
    PushCol(ImGuiCol_WindowBg,             win_bg);
    PushCol(ImGuiCol_ChildBg,              child_bg);
    PushCol(ImGuiCol_PopupBg,              ImVec4(win_bg.x + 0.02f, win_bg.y + 0.02f, win_bg.z + 0.03f, 0.98f));
    PushCol(ImGuiCol_Border,               border_c);
    PushCol(ImGuiCol_FrameBg,              frame_bg);
    PushCol(ImGuiCol_FrameBgHovered,       ImVec4(frame_bg.x + 0.06f, frame_bg.y + 0.07f, frame_bg.z + 0.09f, 1.00f));
    PushCol(ImGuiCol_FrameBgActive,        ImVec4(frame_bg.x + 0.10f, frame_bg.y + 0.12f, frame_bg.z + 0.15f, 1.00f));
    PushCol(ImGuiCol_TitleBg,              win_bg);
    PushCol(ImGuiCol_TitleBgActive,        ImVec4(win_bg.x + 0.03f, win_bg.y + 0.05f, win_bg.z + 0.08f, 1.00f));
    PushCol(ImGuiCol_CheckMark,            pal.accent);
    PushCol(ImGuiCol_SliderGrab,           pal.accent);
    PushCol(ImGuiCol_SliderGrabActive,     ImVec4(1.00f, 1.00f, 1.00f, 0.95f));
    PushCol(ImGuiCol_Button,               ImVec4(pal.accent.x * 0.26f + 0.08f, pal.accent.y * 0.26f + 0.09f, pal.accent.z * 0.26f + 0.12f, 0.95f));
    PushCol(ImGuiCol_ButtonHovered,        ImVec4(pal.accent.x * 0.42f + 0.08f, pal.accent.y * 0.42f + 0.09f, pal.accent.z * 0.42f + 0.12f, 1.00f));
    PushCol(ImGuiCol_ButtonActive,         ImVec4(pal.accent.x * 0.62f + 0.06f, pal.accent.y * 0.62f + 0.07f, pal.accent.z * 0.62f + 0.09f, 1.00f));
    PushCol(ImGuiCol_Header,               ImVec4(pal.accent.x * 0.28f + 0.06f, pal.accent.y * 0.28f + 0.08f, pal.accent.z * 0.28f + 0.10f, 0.88f));
    PushCol(ImGuiCol_HeaderHovered,        ImVec4(pal.accent.x * 0.42f + 0.08f, pal.accent.y * 0.42f + 0.09f, pal.accent.z * 0.42f + 0.12f, 0.96f));
    PushCol(ImGuiCol_HeaderActive,         ImVec4(pal.accent.x * 0.56f + 0.08f, pal.accent.y * 0.56f + 0.09f, pal.accent.z * 0.56f + 0.12f, 1.00f));
    PushCol(ImGuiCol_Tab,                  frame_bg);
    PushCol(ImGuiCol_TabHovered,           ImVec4(pal.accent.x * 0.50f, pal.accent.y * 0.50f, pal.accent.z * 0.50f, 0.95f));
    PushCol(ImGuiCol_TabSelected,          ImVec4(pal.accent.x * 0.38f + 0.06f, pal.accent.y * 0.38f + 0.08f, pal.accent.z * 0.38f + 0.10f, 1.00f));
    PushCol(ImGuiCol_TableHeaderBg,        ImVec4(frame_bg.x + 0.02f, frame_bg.y + 0.02f, frame_bg.z + 0.03f, 1.00f));
    PushCol(ImGuiCol_TableRowBg,           ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
    PushCol(ImGuiCol_TableRowBgAlt,        ImVec4(1.00f, 1.00f, 1.00f, 0.03f));
}

ScopedAdaptiveTheme::~ScopedAdaptiveTheme() {
    ImGui::PopStyleColor(color_count_);
    ImGui::PopStyleVar(var_count_);
}

void ScopedAdaptiveTheme::PushCol(ImGuiCol idx, const ImVec4& col) {
    ImGui::PushStyleColor(idx, col);
    ++color_count_;
}
void ScopedAdaptiveTheme::PushVar(ImGuiStyleVar idx, float val) {
    ImGui::PushStyleVar(idx, val);
    ++var_count_;
}
void ScopedAdaptiveTheme::PushVar(ImGuiStyleVar idx, const ImVec2& val) {
    ImGui::PushStyleVar(idx, val);
    ++var_count_;
}

void DrawSectionVectorIcon(ImDrawList* dl, ImVec2 c, float r, int section_id, ImU32 col) {
    if (!dl) return;
    const float lw = std::max(1.6f, r * 0.22f);
    switch (section_id) {
        case 0: // Дашборд — спидометр/радарная дуга со стрелкой
            dl->AddCircle(c, r, col, 16, lw);
            dl->AddLine(c, ImVec2(c.x + r * 0.62f, c.y - r * 0.52f), col, lw);
            dl->AddCircleFilled(c, lw * 0.9f, col);
            break;
        case 1: // Локальный игрок — силуэт головы и плеч
            dl->AddCircle(ImVec2(c.x, c.y - r * 0.35f), r * 0.38f, col, 12, lw);
            dl->AddBezierQuadratic(ImVec2(c.x - r * 0.80f, c.y + r * 0.80f),
                                   ImVec2(c.x, c.y + r * 0.05f),
                                   ImVec2(c.x + r * 0.80f, c.y + r * 0.80f), col, lw, 10);
            break;
        case 2: // PlayerPool — два силуэта
            dl->AddCircle(ImVec2(c.x - r * 0.28f, c.y - r * 0.30f), r * 0.32f, col, 12, lw);
            dl->AddCircle(ImVec2(c.x + r * 0.38f, c.y - r * 0.22f), r * 0.26f, col, 12, lw);
            dl->AddLine(ImVec2(c.x - r * 0.82f, c.y + r * 0.68f),
                        ImVec2(c.x + r * 0.82f, c.y + r * 0.68f), col, lw);
            break;
        case 3: // VehiclePool — корпус авто + два колеса
            dl->AddRect(ImVec2(c.x - r * 0.88f, c.y - r * 0.18f),
                        ImVec2(c.x + r * 0.88f, c.y + r * 0.42f), col, r * 0.25f, 0, lw);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.48f, c.y + r * 0.58f), r * 0.26f, col);
            dl->AddCircleFilled(ImVec2(c.x + r * 0.48f, c.y + r * 0.58f), r * 0.26f, col);
            break;
        case 4: // Сеть — импульсная диаграмма пакетов
            dl->AddLine(ImVec2(c.x - r * 0.90f, c.y + r * 0.15f), ImVec2(c.x - r * 0.40f, c.y + r * 0.15f), col, lw);
            dl->AddLine(ImVec2(c.x - r * 0.40f, c.y + r * 0.15f), ImVec2(c.x - r * 0.12f, c.y - r * 0.75f), col, lw);
            dl->AddLine(ImVec2(c.x - r * 0.12f, c.y - r * 0.75f), ImVec2(c.x + r * 0.22f, c.y + r * 0.72f), col, lw);
            dl->AddLine(ImVec2(c.x + r * 0.22f, c.y + r * 0.72f), ImVec2(c.x + r * 0.50f, c.y - r * 0.10f), col, lw);
            dl->AddLine(ImVec2(c.x + r * 0.50f, c.y - r * 0.10f), ImVec2(c.x + r * 0.90f, c.y - r * 0.10f), col, lw);
            break;
        case 5: // Лаборатория памяти — микросхема с выводами
            dl->AddRect(ImVec2(c.x - r * 0.58f, c.y - r * 0.58f),
                        ImVec2(c.x + r * 0.58f, c.y + r * 0.58f), col, 2.0f, 0, lw);
            dl->AddLine(ImVec2(c.x - r * 0.95f, c.y - r * 0.25f), ImVec2(c.x - r * 0.58f, c.y - r * 0.25f), col, lw);
            dl->AddLine(ImVec2(c.x - r * 0.95f, c.y + r * 0.25f), ImVec2(c.x - r * 0.58f, c.y + r * 0.25f), col, lw);
            dl->AddLine(ImVec2(c.x + r * 0.58f, c.y - r * 0.25f), ImVec2(c.x + r * 0.95f, c.y - r * 0.25f), col, lw);
            dl->AddLine(ImVec2(c.x + r * 0.58f, c.y + r * 0.25f), ImVec2(c.x + r * 0.95f, c.y + r * 0.25f), col, lw);
            break;
        default: // Настройки и экспорт — три ползунка эквалайзера
            dl->AddLine(ImVec2(c.x - r * 0.80f, c.y - r * 0.52f), ImVec2(c.x + r * 0.80f, c.y - r * 0.52f), col, lw);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.25f, c.y - r * 0.52f), r * 0.24f, col);
            dl->AddLine(ImVec2(c.x - r * 0.80f, c.y), ImVec2(c.x + r * 0.80f, c.y), col, lw);
            dl->AddCircleFilled(ImVec2(c.x + r * 0.32f, c.y), r * 0.24f, col);
            dl->AddLine(ImVec2(c.x - r * 0.80f, c.y + r * 0.52f), ImVec2(c.x + r * 0.80f, c.y + r * 0.52f), col, lw);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.10f, c.y + r * 0.52f), r * 0.24f, col);
            break;
    }
}

void DrawStatusPill(const char* text, bool positive, const ThemePalette& pal) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 tsz = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float pad_x = 8.0f;
    const float pad_y = 3.5f;
    const ImVec2 pmax(pos.x + tsz.x + pad_x * 2.0f, pos.y + tsz.y + pad_y * 2.0f);

    const ImVec4 base = positive ? pal.success : pal.warning;
    const ImU32 bg    = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, 0.18f));
    const ImU32 brd   = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, 0.72f));
    const ImU32 fg    = ImGui::ColorConvertFloat4ToU32(base);

    dl->AddRectFilled(pos, pmax, bg, (tsz.y + pad_y * 2.0f) * 0.5f);
    dl->AddRect(pos, pmax, brd, (tsz.y + pad_y * 2.0f) * 0.5f, 0, 1.2f);
    dl->AddText(ImVec2(pos.x + pad_x, pos.y + pad_y), fg, text);
    ImGui::Dummy(ImVec2(tsz.x + pad_x * 2.0f, tsz.y + pad_y * 2.0f));
}

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
                             const ThemePalette& pal) {
    ImGui::PushID(id);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + card_w, p0.y + card_h);
    const float rounding = 10.0f * ui_scale;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, pal.card_bg_u32, rounding);
    dl->AddRect(p0, p1, pal.card_border_u32, rounding, 0, 1.0f);

    // Верхняя тонкая акцентная линия
    dl->AddLine(ImVec2(p0.x + rounding, p0.y + 1.5f),
                ImVec2(p1.x - rounding, p0.y + 1.5f),
                pal.accent_u32, 2.2f * ui_scale);

    const float pad = 11.0f * ui_scale;
    const ImU32 muted_u32 = ImGui::ColorConvertFloat4ToU32(pal.text_muted);
    const ImU32 text_u32  = ImGui::GetColorU32(ImGuiCol_Text);

    dl->AddText(ImVec2(p0.x + pad, p0.y + pad * 0.75f), muted_u32, title);

    ImFont* font = ImGui::GetFont();
    const float big_sz = font->FontSize * 1.28f;
    dl->AddText(font, big_sz, ImVec2(p0.x + pad, p0.y + pad * 0.75f + font->FontSize + 3.0f * ui_scale),
                text_u32, big_value);

    if (subtitle && subtitle[0] != '\0') {
        dl->AddText(ImVec2(p0.x + pad, p0.y + pad * 0.75f + font->FontSize + big_sz + 6.0f * ui_scale),
                    muted_u32, subtitle);
    }

    // Область спарклайна в нижней трети карточки
    if (ring_values && ring_count >= 4) {
        const float gx0 = p0.x + pad;
        const float gx1 = p1.x - pad;
        const float gy1 = p1.y - pad * 0.75f;
        const float gy0 = std::max(gy1 - card_h * 0.33f, p0.y + card_h * 0.58f);

        float hi = max_scale;
        for (size_t i = 0; i < ring_count; ++i) {
            hi = std::max(hi, ring_values[i]);
        }
        const float span = std::max(0.001f, hi - min_scale);

        ImVec2 prev_pt{};
        for (size_t i = 0; i < ring_count; ++i) {
            const size_t idx = (ring_cursor + i) % ring_count;
            const float norm = std::clamp((ring_values[idx] - min_scale) / span, 0.0f, 1.0f);
            const float x    = gx0 + (gx1 - gx0) * (static_cast<float>(i) / static_cast<float>(ring_count - 1));
            const float y    = gy1 - (gy1 - gy0) * norm;
            const ImVec2 cur_pt(x, y);
            if (i > 0) {
                // Мягкая полупрозрачная заливка под отрезком
                const ImVec2 quad[4] = {prev_pt, cur_pt, ImVec2(cur_pt.x, gy1), ImVec2(prev_pt.x, gy1)};
                dl->AddConvexPolyFilled(quad, 4, pal.accent_fill_u32);
                dl->AddLine(prev_pt, cur_pt, pal.accent_u32, 1.8f * ui_scale);
            }
            prev_pt = cur_pt;
        }
    }

    ImGui::Dummy(ImVec2(card_w, card_h));
    ImGui::PopID();
}

void DrawSmoothProgressBar(const char* label,
                           const char* right_text,
                           float fraction,
                           float height,
                           const ImVec4& fill_color) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    ImGui::TextUnformatted(label);
    if (right_text && right_text[0] != '\0') {
        const float rw = ImGui::CalcTextSize(right_text).x;
        ImGui::SameLine(std::max(120.0f, ImGui::GetWindowContentRegionMax().x - rw));
        ImGui::TextDisabled("%s", right_text);
    }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w   = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 p1(p0.x + w, p0.y + height);
    const float rnd = height * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), rnd);
    if (clamped > 0.004f) {
        const float fw = std::max(height, w * clamped);
        dl->AddRectFilled(p0, ImVec2(p0.x + std::min(w, fw), p1.y),
                          ImGui::ColorConvertFloat4ToU32(fill_color), rnd);
    }
    ImGui::Dummy(ImVec2(w, height + 2.0f));
}

bool DrawFilterChip(const char* label, bool active, const ThemePalette& pal) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(pal.accent.x * 0.45f, pal.accent.y * 0.45f, pal.accent.z * 0.45f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, pal.accent);
    }
    const bool clicked = ImGui::Button(label);
    if (active) {
        ImGui::PopStyleColor(2);
    }
    return clicked;
}

} // namespace br::ui::detail
