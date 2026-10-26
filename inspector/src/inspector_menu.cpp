#include "inspector_menu.hpp"
#include "inspector_ui_helpers.hpp"
#include "safe_memory.hpp"
#include "mod_features.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"

namespace br::ui {

namespace {

struct NavItemMeta {
    const char* short_title;
    const char* full_title;
};

static constexpr NavItemMeta kNavSections[8] = {
    {"Обзор",     "Обзор и Живые Графики"},
    {"Игрок",     "LocalPlayer, CPed и Матрица"},
    {"Игроки",    "PlayerPool (1504 слота)"},
    {"Транспорт", "VehiclePool (2000 слотов)"},
    {"Сеть",      "RakNet, BitStream и Byte-Diff"},
    {"Память",    "Hex, ARM64 Disasm, Watch и Сканер"},
    {"Опции",     "Темы, Адаптация и Экспорт"},
    {"MOD",       "Мод-функции: лечилка, турбо, телепорт, посадка"}
};

bool ReadEntityBasisVectors(uintptr_t matrix_ptr, float out_right[3], float out_fwd[3]) noexcept {
    if (!mem::SafeMemory::IsPlausibleAddress(matrix_ptr)) {
        return false;
    }
    float raw[8]{}; // Right (0..2), pad (3), Forward (4..6), pad (7)
    if (!mem::SafeMemory::ReadBytes(matrix_ptr, raw, sizeof(raw))) {
        return false;
    }
    std::memcpy(out_right, raw + 0, 12);
    std::memcpy(out_fwd,   raw + 4, 12);
    return true;
}

uintptr_t ParseHexVaString(const char* s) noexcept {
    if (!s || !*s) return 0;
    return static_cast<uintptr_t>(std::strtoull(s, nullptr, 16));
}

} // namespace

InspectorMenu::InspectorMenu() {
    std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x0");
    std::snprintf(scan_start_buf_, sizeof(scan_start_buf_), "0x0");
    std::snprintf(scan_query_buf_, sizeof(scan_query_buf_), "7F 45 4C 46");
    pinned_players_.fill(false);
}

void InspectorMenu::SetExternalSnapshotForTest(const inspector::WorldDiagnosticSnapshot& snap) {
    snapshot_              = snap;
    use_external_snapshot_ = true;
}

void InspectorMenu::ApplyTouchKineticScrolling() {
    if (!config_.kinetic_touch_scroll) {
        return;
    }
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win || win->ScrollMax.y <= 0.0f) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool any_widget_dragging = ImGui::IsAnyItemActive() &&
        (ImGui::GetActiveID() != win->MoveId);

    if (hovered && io.MouseDown[0] && !any_widget_dragging && std::fabs(io.MouseDelta.y) > 0.25f) {
        win->Scroll.y = std::clamp(win->Scroll.y - io.MouseDelta.y, 0.0f, win->ScrollMax.y);
        touch_scroll_vel_y_ = -io.MouseDelta.y;
        touch_scroll_win_   = win;
    } else if (!io.MouseDown[0] && touch_scroll_win_ == win && std::fabs(touch_scroll_vel_y_) > 0.35f) {
        win->Scroll.y = std::clamp(win->Scroll.y + touch_scroll_vel_y_, 0.0f, win->ScrollMax.y);
        touch_scroll_vel_y_ *= 0.90f;
        if (win->Scroll.y <= 0.0f || win->Scroll.y >= win->ScrollMax.y) {
            touch_scroll_vel_y_ = 0.0f;
        }
    }
}

void InspectorMenu::ApplyPendingWindowPreset(float ui_scale) {
    if (!pending_preset_apply_) {
        return;
    }
    pending_preset_apply_ = false;

    ImGuiIO& io = ImGui::GetIO();
    const float sx = std::clamp(config_.safe_area_pad_x, 0.0f, 160.0f);
    const float sy = std::clamp(config_.safe_area_pad_y, 0.0f, 120.0f);
    const float avail_w = std::max(480.0f, io.DisplaySize.x - sx * 2.0f);
    const float avail_h = std::max(320.0f, io.DisplaySize.y - sy * 2.0f - 48.0f * ui_scale);
    const float top_y   = sy + 46.0f * ui_scale;

    ImVec2 pos(sx, top_y);
    ImVec2 sz(avail_w * 0.84f, avail_h * 0.86f);

    switch (pending_preset_) {
        case WindowPresetId::CompactFloat:
            sz  = ImVec2(avail_w * 0.64f, avail_h * 0.72f);
            pos = ImVec2(sx + (avail_w - sz.x) * 0.5f, top_y + (avail_h - sz.y) * 0.4f);
            break;
        case WindowPresetId::MaximizedSafe:
            sz  = ImVec2(avail_w, avail_h);
            pos = ImVec2(sx, top_y);
            break;
        case WindowPresetId::SplitLeft:
            sz  = ImVec2(avail_w * 0.49f, avail_h);
            pos = ImVec2(sx, top_y);
            break;
        case WindowPresetId::SplitRight:
            sz  = ImVec2(avail_w * 0.49f, avail_h);
            pos = ImVec2(sx + avail_w - sz.x, top_y);
            break;
        case WindowPresetId::StandardCenter:
        default:
            sz  = ImVec2(avail_w * 0.84f, avail_h * 0.86f);
            pos = ImVec2(sx + (avail_w - sz.x) * 0.5f, top_y + (avail_h - sz.y) * 0.35f);
            break;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
}

void InspectorMenu::RefreshSnapshotAndMetrics(uintptr_t module_base) {
    const uint64_t now = detail::MonotonicNowMs();

    if (!use_external_snapshot_ && !config_.pause_snapshots) {
        const uint64_t interval = static_cast<uint64_t>(std::max(25, config_.snapshot_refresh_ms));
        if (last_snapshot_ms_ == 0 || (now - last_snapshot_ms_) >= interval) {
            inspector::GameReader::CaptureSnapshot(module_base, snapshot_, config_.allow_rakclient_vcall);
            last_snapshot_ms_ = now;

            // Кинематика локального игрока и подсветка изменений кэша пакета 33
            const auto& lp = snapshot_.local_player;
            if (lp.valid && lp.ped_valid) {
                if (has_prev_local_pos_ && now > prev_local_pos_ms_) {
                    const float dt = static_cast<float>(now - prev_local_pos_ms_) * 0.001f;
                    const float dx = lp.pos[0] - prev_local_pos_[0];
                    const float dy = lp.pos[1] - prev_local_pos_[1];
                    const float dz = lp.pos[2] - prev_local_pos_[2];
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist < 250.0f && dt > 0.01f) {
                        computed_speed_ms_  = dist / dt;
                        computed_speed_kmh_ = computed_speed_ms_ * 3.6f;
                        session_odometer_m_ += static_cast<double>(dist);
                    }
                }
                std::memcpy(prev_local_pos_, lp.pos, sizeof(prev_local_pos_));
                prev_local_pos_ms_  = now;
                has_prev_local_pos_ = true;

                for (size_t i = 0; i < 7; ++i) {
                    uint32_t dw = 0;
                    std::memcpy(&dw, lp.cache_block_f0.data() + i * 4, 4);
                    if (dw != prev_sync_cache_33_[i]) {
                        prev_sync_cache_33_[i]    = dw;
                        sync_cache_changed_ms_[i] = now;
                    }
                }
            }
        }
    }

    // Расчёт интенсивности сетевых пакетов раз в 500 мс
    if (prev_net_rate_ms_ == 0 || (now - prev_net_rate_ms_) >= 500ULL) {
        std::vector<net::CapturedNetEvent> tmp_ev;
        std::vector<net::CounterSnapshotEntry> tmp_pc, tmp_rc;
        uint64_t total_pkts = 0, total_rpcs = 0;
        net::NetworkCatalogAndObserver::GetCapturedSnapshot(tmp_ev, tmp_pc, tmp_rc, total_pkts, total_rpcs);
        if (prev_net_rate_ms_ != 0 && now > prev_net_rate_ms_) {
            const float dt = static_cast<float>(now - prev_net_rate_ms_) * 0.001f;
            rate_pkts_per_sec_ = static_cast<float>(total_pkts - prev_net_total_pkts_) / dt;
            rate_rpcs_per_sec_ = static_cast<float>(total_rpcs - prev_net_total_rpcs_) / dt;
        }
        prev_net_total_pkts_ = total_pkts;
        prev_net_total_rpcs_ = total_rpcs;
        prev_net_rate_ms_    = now;
    }

    // Запись в кольцевые буферы живых спарклайнов каждые 80 мс
    if (last_hist_sample_ms_ == 0 || (now - last_hist_sample_ms_) >= 80ULL) {
        ImGuiIO& io = ImGui::GetIO();
        hist_fps_[hist_cursor_]             = io.Framerate;
        hist_frametime_ms_[hist_cursor_]    = io.DeltaTime * 1000.0f;
        hist_ping_ms_[hist_cursor_]         = static_cast<float>(std::max(0, snapshot_.rakclient.last_ping));
        hist_pkt_per_sec_[hist_cursor_]     = rate_pkts_per_sec_ + rate_rpcs_per_sec_;
        hist_local_speed_kmh_[hist_cursor_] = computed_speed_kmh_;
        hist_cursor_ = (hist_cursor_ + 1) % kHistorySamples;
        last_hist_sample_ms_ = now;
    }
}

void InspectorMenu::Render(const InternalImGuiRuntimeTable& rt_table) {
    ImGuiIO& io = ImGui::GetIO();
    const float disp_h   = (io.DisplaySize.y > 120.0f) ? io.DisplaySize.y : 1080.0f;
    const float auto_dpi = std::clamp(disp_h / 1080.0f, 0.82f, 1.85f);
    const float ui_scale = std::clamp(auto_dpi * config_.user_ui_scale, 0.72f, 2.35f);

    if (forced_tab_index_ >= 0 && forced_tab_index_ <= 7) {
        active_section_ = forced_tab_index_;
    }

    // MOD: per-frame тик (турбо) на GL-потоке
    br::mod::Tick();

    RefreshSnapshotAndMetrics(rt_table.module_base);

    detail::ScopedAdaptiveTheme theme_guard(config_.theme, ui_scale, config_.window_bg_alpha);

    if (config_.show_floating_pill) {
        RenderFloatingPill(ui_scale);
    }
    if (config_.show_mini_hud) {
        RenderMiniHud(ui_scale);
    }
    if (config_.menu_open) {
        RenderMainWindow(rt_table, ui_scale);
    }
}

void InspectorMenu::RenderFloatingPill(float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    ImGui::SetNextWindowPos(
        ImVec2(std::max(12.0f, config_.safe_area_pad_x), std::max(8.0f, config_.safe_area_pad_y * 0.5f)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##BRFloatingDockPill", nullptr, flags)) {
        // Удерживаем плавающий бейдж в пределах экрана при поворотах/смене разрешения
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsz  = ImGui::GetWindowSize();
        if (io.DisplaySize.x > 120.0f && io.DisplaySize.y > 120.0f) {
            const float cx = std::clamp(wpos.x, 4.0f, std::max(4.0f, io.DisplaySize.x - wsz.x - 4.0f));
            const float cy = std::clamp(wpos.y, 4.0f, std::max(4.0f, io.DisplaySize.y - wsz.y - 4.0f));
            if (std::fabs(cx - wpos.x) > 1.0f || std::fabs(cy - wpos.y) > 1.0f) {
                ImGui::SetWindowPos(ImVec2(cx, cy));
            }
        }

        ImGui::TextColored(pal.accent, "BR 16.111");
        ImGui::SameLine();
        if (ImGui::Button(config_.menu_open ? "Скрыть" : "Меню")) {
            config_.menu_open = !config_.menu_open;
        }
        ImGui::SameLine();
        if (detail::DrawFilterChip("HUD", config_.show_mini_hud, pal)) {
            config_.show_mini_hud = !config_.show_mini_hud;
        }
        ImGui::SameLine();
        if (detail::DrawFilterChip(config_.pause_snapshots ? "ПАУЗА" : "LIVE", !config_.pause_snapshots, pal)) {
            config_.pause_snapshots = !config_.pause_snapshots;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f FPS | %d мс | Игр:%zu Авто:%zu",
                            io.Framerate,
                            snapshot_.rakclient.last_ping,
                            snapshot_.remote_players.size(),
                            snapshot_.vehicles.size());
    }
    ImGui::End();
}

void InspectorMenu::RenderMiniHud(float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    const float pad_x = std::max(14.0f, config_.safe_area_pad_x);
    const float pad_y = std::max(14.0f, config_.safe_area_pad_y);
    ImGui::SetNextWindowPos(
        ImVec2(std::max(16.0f, io.DisplaySize.x - 320.0f * ui_scale - pad_x), pad_y),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.80f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##BRTelemetryMiniHud", nullptr, flags)) {
        ImGui::TextColored(pal.accent, "BR 16.111.19050 | %.0f FPS (%.1f мс)",
                           io.Framerate, io.DeltaTime * 1000.0f);
        ImGui::Separator();

        const auto& lp = snapshot_.local_player;
        if (lp.valid) {
            ImGui::Text("ID %u | %s | %.0f км/ч",
                        lp.player_id,
                        lp.name.empty() ? "-" : lp.name.c_str(),
                        computed_speed_kmh_);
            if (lp.ped_valid) {
                float r_vec[3]{}, f_vec[3]{0.0f, 1.0f, 0.0f};
                ReadEntityBasisVectors(lp.matrix_ptr, r_vec, f_vec);
                const float yaw = std::atan2(f_vec[1], f_vec[0]) * (180.0f / 3.14159265f);
                ImGui::Text("XYZ: %.1f, %.1f, %.1f (%s %.0f°)",
                            lp.pos[0], lp.pos[1], lp.pos[2],
                            detail::CompassDirectionRu(yaw), yaw);
            }
            if (lp.local_vehicle_ptr != 0) {
                ImGui::TextColored(pal.success, "Авто: VehID %u (подтип %u)",
                                   lp.local_vehicle_id, lp.local_vehicle_subtype);
            }
        } else {
            ImGui::TextDisabled("Ожидание входа на сервер...");
        }

        ImGui::Text("Пулы: P %zu/1504 | V %zu/2000 | O %zu/4096",
                    snapshot_.remote_players.size(),
                    snapshot_.vehicles.size(),
                    snapshot_.active_object_slots);
        ImGui::TextDisabled("Сеть: Ping %d мс | %.1f пкт/с",
                            snapshot_.rakclient.last_ping,
                            rate_pkts_per_sec_ + rate_rpcs_per_sec_);
    }
    ImGui::End();
}

void InspectorMenu::RenderNavigationSidebar(float ui_scale, float sidebar_w) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    if (ImGui::BeginChild("##AdaptiveSidebarRail", ImVec2(sidebar_w, 0.0f), ImGuiChildFlags_Border)) {
        ApplyTouchKineticScrolling();

        const float item_h = 44.0f * ui_scale;
        const float avail_w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (int i = 0; i < 8; ++i) {
            ImGui::PushID(i);
            const bool selected = (active_section_ == i);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 p1(p0.x + avail_w, p0.y + item_h);

            if (ImGui::InvisibleButton("##NavBtn", ImVec2(avail_w, item_h))) {
                active_section_   = i;
                forced_tab_index_ = -1;
            }
            const bool hovered = ImGui::IsItemHovered();

            if (selected || hovered) {
                const ImU32 bg = selected
                    ? ImGui::ColorConvertFloat4ToU32(ImVec4(pal.accent.x * 0.28f, pal.accent.y * 0.28f, pal.accent.z * 0.28f, 0.85f))
                    : ImGui::ColorConvertFloat4ToU32(ImVec4(pal.accent.x * 0.14f, pal.accent.y * 0.14f, pal.accent.z * 0.14f, 0.55f));
                dl->AddRectFilled(p0, p1, bg, 8.0f * ui_scale);
            }
            if (selected) {
                // Левая вертикальная светящаяся полоса активного раздела
                dl->AddRectFilled(ImVec2(p0.x, p0.y + 6.0f * ui_scale),
                                  ImVec2(p0.x + 3.6f * ui_scale, p1.y - 6.0f * ui_scale),
                                  pal.accent_u32, 2.0f * ui_scale);
            }

            const ImU32 icon_col = selected ? pal.accent_u32 : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 txt_col  = selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);

            detail::DrawSectionVectorIcon(
                dl,
                ImVec2(p0.x + 21.0f * ui_scale, (p0.y + p1.y) * 0.5f),
                8.5f * ui_scale, i, icon_col);

            const float th = ImGui::GetFontSize();
            dl->AddText(ImVec2(p0.x + 38.0f * ui_scale, (p0.y + p1.y - th) * 0.5f),
                        txt_col, kNavSections[i].short_title);

            // Живые счётчики справа у пунктов Игроки / Транспорт / Память
            char badge[24]{};
            if (i == 2) std::snprintf(badge, sizeof(badge), "%zu", snapshot_.remote_players.size());
            else if (i == 3) std::snprintf(badge, sizeof(badge), "%zu", snapshot_.vehicles.size());
            else if (i == 5 && !watch_items_.empty()) std::snprintf(badge, sizeof(badge), "%zu", watch_items_.size());

            if (badge[0] != '\0') {
                const ImVec2 bsz = ImGui::CalcTextSize(badge);
                dl->AddText(ImVec2(p1.x - bsz.x - 8.0f * ui_scale, (p0.y + p1.y - bsz.y) * 0.5f),
                            selected ? pal.accent_u32 : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            badge);
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Опрос пулов:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##RefreshMsSide", &config_.snapshot_refresh_ms, 30, 600, "%d мс");
    }
    ImGui::EndChild();
}

void InspectorMenu::RenderNavigationTopBar(float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);
    for (int i = 0; i < 8; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 5.0f * ui_scale);
        if (detail::DrawFilterChip(kNavSections[i].short_title, active_section_ == i, pal)) {
            active_section_   = i;
            forced_tab_index_ = -1;
        }
    }
    ImGui::Separator();
}

void InspectorMenu::RenderMainWindow(const InternalImGuiRuntimeTable& rt_table, float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    const float sx = std::clamp(config_.safe_area_pad_x, 0.0f, 160.0f);
    const float sy = std::clamp(config_.safe_area_pad_y, 0.0f, 120.0f);
    const float def_w = std::min(1040.0f * ui_scale, std::max(500.0f, (io.DisplaySize.x - sx * 2.0f) * 0.86f));
    const float def_h = std::min(670.0f * ui_scale,  std::max(360.0f, (io.DisplaySize.y - sy * 2.0f) * 0.84f));

    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 300.0f),
                                        ImVec2(std::max(600.0f, io.DisplaySize.x), std::max(400.0f, io.DisplaySize.y)));
    ImGui::SetNextWindowSize(ImVec2(def_w, def_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(sx + 24.0f * ui_scale, sy + 52.0f * ui_scale), ImGuiCond_FirstUseEver);

    ApplyPendingWindowPreset(ui_scale);

    if (!ImGui::Begin("BLACK RUSSIA 16.111.19050 — Инспектор и Анализатор (Read-Only)###BRInspectorMain",
                      &config_.menu_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    // Верхняя адаптивная шапка быстрых действий
    detail::DrawStatusPill(rt_table.build_id_verified ? "16.111.19050 OK" : "ДИАГН. РЕЖИМ",
                           rt_table.build_id_verified, pal);
    ImGui::SameLine();
    ImGui::TextColored(pal.accent, "%s", kNavSections[std::clamp(active_section_, 0, 7)].full_title);

    const float right_btns_w = 275.0f * ui_scale;
    if (ImGui::GetWindowWidth() > right_btns_w + 340.0f * ui_scale) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - right_btns_w);
        if (ImGui::SmallButton(config_.pause_snapshots ? "Продолжить" : "Пауза")) {
            config_.pause_snapshots = !config_.pause_snapshots;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Компакт")) {
            pending_preset_ = WindowPresetId::CompactFloat;
            pending_preset_apply_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Макс")) {
            pending_preset_ = WindowPresetId::MaximizedSafe;
            pending_preset_apply_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("JSON")) {
            ExportWorldSnapshotJson(rt_table);
        }
    }
    ImGui::Separator();

    const float win_w = ImGui::GetContentRegionAvail().x;
    const bool use_sidebar = !config_.adaptive_sidebar || (win_w >= 690.0f * ui_scale);

    if (use_sidebar) {
        const float sidebar_w = std::clamp(win_w * 0.20f, 155.0f * ui_scale, 210.0f * ui_scale);
        RenderNavigationSidebar(ui_scale, sidebar_w);
        ImGui::SameLine();
    } else {
        RenderNavigationTopBar(ui_scale);
    }

    if (ImGui::BeginChild("##BRMainSectionContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        switch (active_section_) {
            case 0: RenderSectionDashboard(rt_table, ui_scale); break;
            case 1: RenderSectionLocalPlayer(ui_scale); break;
            case 2: RenderSectionPlayerPool(ui_scale); break;
            case 3: RenderSectionVehiclePool(ui_scale); break;
            case 4: RenderSectionNetwork(rt_table.module_base, ui_scale); break;
            case 5: RenderSectionMemoryLab(rt_table, ui_scale); break;
            case 7: RenderSectionMod(ui_scale); break;
            case 6:
            default: RenderSectionSettingsAndExport(rt_table, ui_scale); break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void InspectorMenu::RenderSectionDashboard(const InternalImGuiRuntimeTable& rt_table, float ui_scale) {
    ApplyTouchKineticScrolling();
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);
    ImGuiIO& io    = ImGui::GetIO();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float gap     = ImGui::GetStyle().ItemSpacing.x;
    const int   cols    = (avail_w >= 760.0f * ui_scale) ? 4 : (avail_w >= 420.0f * ui_scale) ? 2 : 1;
    const float card_w  = (avail_w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float card_h  = 118.0f * ui_scale;

    char v_fps[32]{}, s_fps[48]{};
    std::snprintf(v_fps, sizeof(v_fps), "%.0f FPS", io.Framerate);
    std::snprintf(s_fps, sizeof(s_fps), "%.2f мс | %.0fx%.0f", io.DeltaTime * 1000.0f, io.DisplaySize.x, io.DisplaySize.y);

    char v_net[32]{}, s_net[48]{};
    std::snprintf(v_net, sizeof(v_net), "%d мс", snapshot_.rakclient.last_ping);
    std::snprintf(s_net, sizeof(s_net), "%.1f пкт/с | MTU %d",
                  rate_pkts_per_sec_ + rate_rpcs_per_sec_, snapshot_.rakclient.mtu_size);

    char v_spd[32]{}, s_spd[48]{};
    std::snprintf(v_spd, sizeof(v_spd), "%.1f км/ч", computed_speed_kmh_);
    std::snprintf(s_spd, sizeof(s_spd), "%.1f м/с | Путь %.0f м", computed_speed_ms_, session_odometer_m_);

    char v_pls[32]{}, s_pls[48]{};
    std::snprintf(v_pls, sizeof(v_pls), "%zu игр.", snapshot_.remote_players.size());
    std::snprintf(s_pls, sizeof(s_pls), "Авто: %zu | Объекты: %zu",
                  snapshot_.vehicles.size(), snapshot_.active_object_slots);

    detail::DrawMetricSparklineCard("c_fps", "РЕНДЕР GLES 3.0", v_fps, s_fps,
                                    hist_fps_.data(), kHistorySamples, hist_cursor_,
                                    0.0f, 120.0f, card_w, card_h, ui_scale, pal);
    if (cols >= 2) ImGui::SameLine();
    detail::DrawMetricSparklineCard("c_net", "СЕТЬ RAKNET", v_net, s_net,
                                    hist_ping_ms_.data(), kHistorySamples, hist_cursor_,
                                    0.0f, 200.0f, card_w, card_h, ui_scale, pal);
    if (cols == 4) ImGui::SameLine();
    detail::DrawMetricSparklineCard("c_spd", "ДИНАМИКА ИГРОКА", v_spd, s_spd,
                                    hist_local_speed_kmh_.data(), kHistorySamples, hist_cursor_,
                                    0.0f, 220.0f, card_w, card_h, ui_scale, pal);
    if (cols >= 2) ImGui::SameLine();
    detail::DrawMetricSparklineCard("c_pls", "АКТИВНОСТЬ МИРА", v_pls, s_pls,
                                    hist_pkt_per_sec_.data(), kHistorySamples, hist_cursor_,
                                    0.0f, 40.0f, card_w, card_h, ui_scale, pal);

    ImGui::Spacing();
    ImGui::TextColored(pal.accent, "Заполненность игровых пулов (16.111.19050)");
    char r_pl[32]{}, r_vh[32]{}, r_ob[32]{};
    std::snprintf(r_pl, sizeof(r_pl), "%zu / 1504", snapshot_.remote_players.size());
    std::snprintf(r_vh, sizeof(r_vh), "%zu / 2000", snapshot_.vehicles.size());
    std::snprintf(r_ob, sizeof(r_ob), "%zu / 4096", snapshot_.active_object_slots);

    detail::DrawSmoothProgressBar("PlayerPool (VA +0x036130B0)", r_pl,
                                  static_cast<float>(snapshot_.remote_players.size()) / 1504.0f,
                                  9.0f * ui_scale, pal.accent);
    detail::DrawSmoothProgressBar("VehiclePool (VA +0x036130B8)", r_vh,
                                  static_cast<float>(snapshot_.vehicles.size()) / 2000.0f,
                                  9.0f * ui_scale, pal.success);
    detail::DrawSmoothProgressBar("ObjectPool (VA +0x036130D0)", r_ob,
                                  static_cast<float>(snapshot_.active_object_slots) / 4096.0f,
                                  9.0f * ui_scale, pal.warning);

    ImGui::Spacing();
    ImGui::TextColored(pal.accent, "Быстрые переходы к живым структурам в памяти");
    const uintptr_t base = rt_table.module_base;
    if (ImGui::Button("CLocalPlayer -> Hex") && snapshot_.local_player.local_player_ptr != 0) {
        JumpToMemoryAddress(snapshot_.local_player.local_player_ptr, "CLocalPlayer", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("CPed -> Hex") && snapshot_.local_player.ped_ptr != 0) {
        JumpToMemoryAddress(snapshot_.local_player.ped_ptr, "Local CPed", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("RakClient -> Hex") && snapshot_.rakclient.rakclient_ptr != 0) {
        JumpToMemoryAddress(snapshot_.rakclient.rakclient_ptr, "RakClientInterface", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("JNILib::step -> ASM") && base != 0) {
        JumpToMemoryAddress(base + offsets::jni_bridge::kJni_JNILib_step, "JNILib::step", true);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Base: 0x%" PRIXPTR " | Own ImGuiContext: 0x%" PRIXPTR " | Рендер: %s",
                        base,
                        reinterpret_cast<uintptr_t>(ImGui::GetCurrentContext()),
                        rt_table.render_hook_name ? rt_table.render_hook_name : "-");
}

void InspectorMenu::RenderSectionLocalPlayer(float ui_scale) {
    ApplyTouchKineticScrolling();
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);
    const auto& lp = snapshot_.local_player;

    if (!lp.valid) {
        ImGui::TextDisabled("CLocalPlayer ещё не создан (зайдите на игровой сервер).");
        return;
    }

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const bool two_col  = (avail_w >= 640.0f * ui_scale);
    const float col_w   = two_col ? (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f : avail_w;

    if (ImGui::BeginChild("##LP_LeftCard", ImVec2(col_w, two_col ? 0.0f : 310.0f * ui_scale), ImGuiChildFlags_Border)) {
        ApplyTouchKineticScrolling();
        ImGui::TextColored(pal.accent, "CLocalPlayer и Кинематика CPed");
        ImGui::Separator();

        ImGui::Text("PlayerID (+0x00): %u | Ник (+0x02): %s",
                    lp.player_id, lp.name.empty() ? "-" : lp.name.c_str());
        ImGui::Text("CLocalPlayer*: 0x%" PRIXPTR " | CPed*: 0x%" PRIXPTR,
                    lp.local_player_ptr, lp.ped_ptr);

        if (ImGui::SmallButton("Hex LocalPlayer")) JumpToMemoryAddress(lp.local_player_ptr, "CLocalPlayer", false);
        ImGui::SameLine();
        if (lp.ped_ptr != 0 && ImGui::SmallButton("Hex CPed")) JumpToMemoryAddress(lp.ped_ptr, "Local CPed", false);
        ImGui::SameLine();
        if (lp.ped_ptr != 0 && ImGui::SmallButton("+ Watch Pos")) {
            AddWatchAddress(lp.ped_ptr + offsets::player::kPedPosXOffset, WatchValueType::Vec3f, "LocalPlayer XYZ");
        }

        if (lp.ped_valid) {
            float m_right[3]{1.0f, 0.0f, 0.0f}, m_fwd[3]{0.0f, 1.0f, 0.0f};
            const bool mat_ok = ReadEntityBasisVectors(lp.matrix_ptr, m_right, m_fwd);
            const float yaw = std::atan2(m_fwd[1], m_fwd[0]) * (180.0f / 3.14159265f);
            ImGui::Spacing();
            ImGui::Text("Позиция XYZ: (%.3f, %.3f, %.3f)", lp.pos[0], lp.pos[1], lp.pos[2]);
            ImGui::Text("Курс: %.1f° (%s) | Скорость: %.2f м/с (%.1f км/ч)",
                        yaw, detail::CompassDirectionRu(yaw), computed_speed_ms_, computed_speed_kmh_);
            ImGui::Text("Одометр сессии: %.1f м", session_odometer_m_);
            ImGui::SameLine();
            if (ImGui::SmallButton("Сброс пути")) session_odometer_m_ = 0.0;

            char s_a[32]{}, s_b[32]{};
            std::snprintf(s_a, sizeof(s_a), "%.1f (u16: %u)", lp.ped_float_4b0, static_cast<uint16_t>(std::max(0.0f, lp.ped_float_4b0)));
            std::snprintf(s_b, sizeof(s_b), "%.1f (u16: %u)", lp.ped_float_4b8, static_cast<uint16_t>(std::max(0.0f, lp.ped_float_4b8)));
            detail::DrawSmoothProgressBar("Поле CPed +0x4B0", s_a, lp.ped_float_4b0 / 100.0f, 8.5f * ui_scale, pal.success);
            detail::DrawSmoothProgressBar("Поле CPed +0x4B8", s_b, lp.ped_float_4b8 / 100.0f, 8.5f * ui_scale, pal.accent);

            ImGui::Spacing();
            ImGui::TextDisabled("Векторы CMatrix (+0x48 -> 0x%" PRIXPTR "):", lp.matrix_ptr);
            if (mat_ok) {
                ImGui::BulletText("Right:   (%.3f, %.3f, %.3f)", m_right[0], m_right[1], m_right[2]);
                ImGui::BulletText("Forward: (%.3f, %.3f, %.3f)", m_fwd[0], m_fwd[1], m_fwd[2]);
            }
        }
    }
    ImGui::EndChild();

    if (two_col) ImGui::SameLine();

    if (ImGui::BeginChild("##LP_RightCard", ImVec2(0.0f, two_col ? 0.0f : 310.0f * ui_scale), ImGuiChildFlags_Border)) {
        ApplyTouchKineticScrolling();
        ImGui::TextColored(pal.accent, "Сетевой кэш (Пакеты 30/33) и Текущий Транспорт");
        ImGui::Separator();

        float sync_200_f = 0.0f;
        std::memcpy(&sync_200_f, &lp.sync_field_200, 4);
        ImGui::Text("Поля пакета 30 (+0x1F4 / +0x1F8 / +0x200): 0x%X / 0x%X / %.2f",
                    lp.sync_field_1f4, lp.sync_field_1f8, sync_200_f);

        const uint64_t now_ms = detail::MonotonicNowMs();
        ImGui::TextDisabled("Кэш пакета 33 (+0xF0..+0x10B, живая подсветка изменений):");
        for (size_t i = 0; i < 7; ++i) {
            uint32_t dw = 0;
            std::memcpy(&dw, lp.cache_block_f0.data() + i * 4, 4);
            const bool changed = (sync_cache_changed_ms_[i] != 0 && (now_ms - sync_cache_changed_ms_[i]) < 1200ULL);
            if (changed) {
                ImGui::TextColored(pal.warning, "  [+0x%02zX] = 0x%08X [ИЗМ]", 0xF0 + i * 4, dw);
            } else {
                ImGui::Text("  [+0x%02zX] = 0x%08X", 0xF0 + i * 4, dw);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (lp.local_vehicle_ptr != 0) {
            const float f790 = mem::SafeMemory::ReadOr<float>(
                lp.local_vehicle_ptr + offsets::vehicle::kVehSpecialFloat790Offset, 0.0f);
            ImGui::TextColored(pal.success, "Текущий CVehicle*: 0x%" PRIXPTR, lp.local_vehicle_ptr);
            ImGui::Text("VehID (+0x434): %u | Подтип (+0x430): %u",
                        lp.local_vehicle_id, lp.local_vehicle_subtype);
            ImGui::Text("Поле +0x790: %.2f | CPed.inVeh (+0x548): %u",
                        f790, lp.ped_in_vehicle_flag);
            if (ImGui::SmallButton("Открыть CVehicle в Hex")) {
                JumpToMemoryAddress(lp.local_vehicle_ptr, "Local CVehicle", false);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Watch +0x790")) {
                AddWatchAddress(lp.local_vehicle_ptr + offsets::vehicle::kVehSpecialFloat790Offset,
                                WatchValueType::Float32, "LocalVeh +0x790");
            }
        } else {
            ImGui::TextDisabled("Персонаж сейчас пешком (CVehicle* == NULL).");
        }
    }
    ImGui::EndChild();
}

void InspectorMenu::RenderSectionPlayerPool(float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    ImGui::SetNextItemWidth(200.0f * ui_scale);
    ImGui::InputText("Поиск ника/ID", player_search_buf_, sizeof(player_search_buf_));
    ImGui::SameLine();
    if (detail::DrawFilterChip("★ Избранные", filter_players_pinned_, pal)) filter_players_pinned_ = !filter_players_pinned_;
    ImGui::SameLine();
    if (detail::DrawFilterChip("С CPed*", filter_players_ped_, pal)) filter_players_ped_ = !filter_players_ped_;
    ImGui::SameLine();
    if (detail::DrawFilterChip("В авто", filter_players_veh_, pal)) filter_players_veh_ = !filter_players_veh_;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(135.0f * ui_scale);
    ImGui::SliderFloat("Дист.", &player_max_dist_m_, 20.0f, 3000.0f, "< %.0f м");

    std::vector<const inspector::RemotePlayerEntry*> rows;
    rows.reserve(snapshot_.remote_players.size());
    for (const auto& rp : snapshot_.remote_players) {
        const bool is_pin = (rp.slot_id < 1504) && pinned_players_[rp.slot_id];
        if (filter_players_pinned_ && !is_pin) continue;
        if (filter_players_ped_ && !rp.ped_valid) continue;
        if (filter_players_veh_ && rp.ped_in_vehicle == 0) continue;
        if (rp.ped_valid && player_max_dist_m_ < 2995.0f && rp.distance_to_local > player_max_dist_m_) continue;

        if (player_search_buf_[0] != '\0') {
            char id_s[16]{};
            std::snprintf(id_s, sizeof(id_s), "%u", rp.slot_id);
            if (!detail::ContainsCaseInsensitive(rp.name, player_search_buf_) &&
                !detail::ContainsCaseInsensitive(id_s, player_search_buf_)) {
                continue;
            }
        }
        rows.push_back(&rp);
    }

    const float avail_w  = ImGui::GetContentRegionAvail().x;
    const bool has_sel   = (selected_remote_slot_ >= 0);
    const bool side_card = has_sel && (avail_w >= 700.0f * ui_scale);
    const float tbl_w    = side_card ? (avail_w * 0.62f) : 0.0f;
    const float tbl_h    = (has_sel && !side_card) ? (ImGui::GetContentRegionAvail().y * 0.56f) : 0.0f;

    if (ImGui::BeginTable("##PlayersAdaptiveTbl", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(tbl_w, tbl_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("★",      ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 28.0f * ui_scale);
        ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 50.0f * ui_scale);
        ImGui::TableSetupColumn("Никнейм",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Дист.",  ImGuiTableColumnFlags_WidthFixed, 72.0f * ui_scale);
        ImGui::TableSetupColumn("+4B0",   ImGuiTableColumnFlags_WidthFixed, 56.0f * ui_scale);
        ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 68.0f * ui_scale);
        ImGui::TableSetupColumn("Hex",    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 48.0f * ui_scale);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            const int col = (specs->SpecsCount > 0) ? specs->Specs[0].ColumnIndex : 1;
            const bool asc = (specs->SpecsCount == 0) || (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            std::sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
                const bool pa = (a->slot_id < 1504) && pinned_players_[a->slot_id];
                const bool pb = (b->slot_id < 1504) && pinned_players_[b->slot_id];
                if (pa != pb) return pa > pb; // Избранные ★ всегда закреплены наверху!
                int cmp = 0;
                if (col == 2) cmp = a->name.compare(b->name);
                else if (col == 3) cmp = (a->distance_to_local < b->distance_to_local) ? -1 : (a->distance_to_local > b->distance_to_local ? 1 : 0);
                else if (col == 4) cmp = (a->ped_float_4b0 < b->ped_float_4b0) ? -1 : 1;
                else cmp = static_cast<int>(a->slot_id) - static_cast<int>(b->slot_id);
                return asc ? (cmp < 0) : (cmp > 0);
            });
        }

        ApplyTouchKineticScrolling();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto* rp = rows[static_cast<size_t>(r)];
                const bool pinned = (rp->slot_id < 1504) && pinned_players_[rp->slot_id];
                const bool is_sel = (selected_remote_slot_ == static_cast<int>(rp->slot_id));

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(rp->slot_id));

                ImGui::TableSetColumnIndex(0);
                if (ImGui::SmallButton(pinned ? "★" : "☆") && rp->slot_id < 1504) {
                    pinned_players_[rp->slot_id] = !pinned_players_[rp->slot_id];
                }

                ImGui::TableSetColumnIndex(1);
                char id_lbl[16]{};
                std::snprintf(id_lbl, sizeof(id_lbl), "%u", rp->slot_id);
                if (ImGui::Selectable(id_lbl, is_sel, ImGuiSelectableFlags_None)) {
                    selected_remote_slot_ = is_sel ? -1 : static_cast<int>(rp->slot_id);
                }

                ImGui::TableSetColumnIndex(2);
                if (pinned) ImGui::TextColored(pal.warning, "%s", rp->name.c_str());
                else        ImGui::TextUnformatted(rp->name.c_str());

                ImGui::TableSetColumnIndex(3);
                if (rp->ped_valid) ImGui::Text("%.1f м", rp->distance_to_local);
                else               ImGui::TextDisabled("стрим-аут");

                ImGui::TableSetColumnIndex(4);
                if (rp->ped_valid) ImGui::Text("%.0f", rp->ped_float_4b0);
                else               ImGui::TextDisabled("-");

                ImGui::TableSetColumnIndex(5);
                if (!rp->ped_valid)           ImGui::TextDisabled("Нет Ped");
                else if (rp->ped_in_vehicle)  ImGui::TextColored(pal.success, "В авто");
                else                          ImGui::TextUnformatted("Пешком");

                ImGui::TableSetColumnIndex(6);
                if (ImGui::SmallButton("Hex")) {
                    JumpToMemoryAddress(rp->remote_ptr, rp->name.c_str(), false);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    if (has_sel) {
        if (side_card) ImGui::SameLine();
        const inspector::RemotePlayerEntry* sel = nullptr;
        for (const auto& rp : snapshot_.remote_players) {
            if (static_cast<int>(rp.slot_id) == selected_remote_slot_) {
                sel = &rp;
                break;
            }
        }
        if (ImGui::BeginChild("##RemoteDetailCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            ApplyTouchKineticScrolling();
            if (!sel) {
                ImGui::TextDisabled("Слот #%d освободился.", selected_remote_slot_);
                if (ImGui::SmallButton("Закрыть")) selected_remote_slot_ = -1;
            } else {
                ImGui::TextColored(pal.accent, "Игрок [%u] %s", sel->slot_id, sel->name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Закрыть")) selected_remote_slot_ = -1;
                ImGui::Separator();

                ImGui::Text("CRemotePlayer*: 0x%" PRIXPTR " (ID +0x4A: %u)", sel->remote_ptr, sel->reported_id);
                ImGui::Text("CPed* (+0x10):  0x%" PRIXPTR, sel->ped_ptr);
                ImGui::Text("Pool Aux (+0x2F30 / +0x46B0): %d / %d", sel->pool_aux_a, sel->pool_aux_b);
                ImGui::Text("Поля +0x120 / +0x150 / +0x15B / +0x15C: 0x%X / 0x%" PRIX64 " / %u / %u",
                            sel->state_120, sel->field_150, sel->byte_15b, sel->u16_15c);

                if (sel->ped_valid) {
                    const auto& lp = snapshot_.local_player;
                    ImGui::Spacing();
                    ImGui::Text("XYZ: (%.2f, %.2f, %.2f) | Дист: %.1f м",
                                sel->pos[0], sel->pos[1], sel->pos[2], sel->distance_to_local);
                    if (lp.valid && lp.ped_valid) {
                        ImGui::TextDisabled("ΔXYZ от вас: (%+.1f, %+.1f, %+.1f)",
                                            sel->pos[0] - lp.pos[0],
                                            sel->pos[1] - lp.pos[1],
                                            sel->pos[2] - lp.pos[2]);
                    }
                    ImGui::Text("CPed +0x4B0 / +0x4B8: %.1f / %.1f | CVehicle*: 0x%" PRIXPTR,
                                sel->ped_float_4b0, sel->ped_float_4b8, sel->ped_vehicle_ptr);
                }

                if (ImGui::SmallButton("CRemotePlayer -> Hex")) JumpToMemoryAddress(sel->remote_ptr, sel->name.c_str(), false);
                if (sel->ped_ptr != 0) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("CPed -> Hex")) JumpToMemoryAddress(sel->ped_ptr, "Remote CPed", false);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Watch XYZ")) {
                        AddWatchAddress(sel->ped_ptr + offsets::player::kPedPosXOffset, WatchValueType::Vec3f, sel->name.c_str());
                    }
                }
            }
        }
        ImGui::EndChild();
    }
}

void InspectorMenu::RenderSectionVehiclePool(float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    ImGui::SetNextItemWidth(160.0f * ui_scale);
    ImGui::InputText("VehID / Слот", vehicle_search_buf_, sizeof(vehicle_search_buf_));
    ImGui::SameLine();
    if (detail::DrawFilterChip("Все", vehicle_quick_preset_ == 0, pal)) vehicle_quick_preset_ = 0;
    ImGui::SameLine();
    if (detail::DrawFilterChip("< 150 м", vehicle_quick_preset_ == 1, pal)) vehicle_quick_preset_ = 1;
    ImGui::SameLine();
    if (detail::DrawFilterChip("В движении", vehicle_quick_preset_ == 2, pal)) vehicle_quick_preset_ = 2;
    ImGui::SameLine();
    if (detail::DrawFilterChip("Подтип 11", vehicle_quick_preset_ == 3, pal)) vehicle_quick_preset_ = 3;
    ImGui::SameLine();
    if (detail::DrawFilterChip("С CVehicle*", vehicle_quick_preset_ == 4, pal)) vehicle_quick_preset_ = 4;

    std::vector<const inspector::VehicleEntry*> rows;
    rows.reserve(snapshot_.vehicles.size());
    for (const auto& v : snapshot_.vehicles) {
        if (vehicle_quick_preset_ == 1 && (!v.game_vehicle_valid || v.distance_to_local > 150.0f)) continue;
        if (vehicle_quick_preset_ == 2 && (!v.game_vehicle_valid || v.speed_magnitude * 3.6f < 1.5f)) continue;
        if (vehicle_quick_preset_ == 3 && v.class_subtype != 11) continue;
        if (vehicle_quick_preset_ == 4 && !v.game_vehicle_valid) continue;

        if (vehicle_search_buf_[0] != '\0') {
            char tmp[32]{};
            std::snprintf(tmp, sizeof(tmp), "%u %u %u", v.slot_index, v.vehicle_id, v.class_subtype);
            if (!detail::ContainsCaseInsensitive(tmp, vehicle_search_buf_)) continue;
        }
        rows.push_back(&v);
    }

    const float avail_w  = ImGui::GetContentRegionAvail().x;
    const bool has_sel   = (selected_vehicle_slot_ >= 0);
    const bool side_card = has_sel && (avail_w >= 700.0f * ui_scale);
    const float tbl_w    = side_card ? (avail_w * 0.62f) : 0.0f;
    const float tbl_h    = (has_sel && !side_card) ? (ImGui::GetContentRegionAvail().y * 0.56f) : 0.0f;

    if (ImGui::BeginTable("##VehiclesAdaptiveTbl", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(tbl_w, tbl_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Слот",    ImGuiTableColumnFlags_WidthFixed, 54.0f * ui_scale);
        ImGui::TableSetupColumn("VehID",   ImGuiTableColumnFlags_WidthFixed, 60.0f * ui_scale);
        ImGui::TableSetupColumn("Подтип",  ImGuiTableColumnFlags_WidthFixed, 64.0f * ui_scale);
        ImGui::TableSetupColumn("Скорость",ImGuiTableColumnFlags_WidthFixed, 86.0f * ui_scale);
        ImGui::TableSetupColumn("Дист.",   ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 76.0f * ui_scale);
        ImGui::TableSetupColumn("Координаты XYZ", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hex",     ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 48.0f * ui_scale);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            const int col = (specs->SpecsCount > 0) ? specs->Specs[0].ColumnIndex : 4;
            const bool asc = (specs->SpecsCount == 0) || (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            std::sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
                if (a->is_local_vehicle != b->is_local_vehicle) return a->is_local_vehicle > b->is_local_vehicle;
                float diff = 0.0f;
                if (col == 0) diff = static_cast<float>(a->slot_index) - static_cast<float>(b->slot_index);
                else if (col == 1) diff = static_cast<float>(a->vehicle_id) - static_cast<float>(b->vehicle_id);
                else if (col == 2) diff = static_cast<float>(a->class_subtype) - static_cast<float>(b->class_subtype);
                else if (col == 3) diff = a->speed_magnitude - b->speed_magnitude;
                else diff = a->distance_to_local - b->distance_to_local;
                return asc ? (diff < 0.0f) : (diff > 0.0f);
            });
        }

        ApplyTouchKineticScrolling();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto* v = rows[static_cast<size_t>(r)];
                const bool is_sel = (selected_vehicle_slot_ == static_cast<int>(v->slot_index));

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(v->slot_index));

                ImGui::TableSetColumnIndex(0);
                char sl[16]{};
                std::snprintf(sl, sizeof(sl), "%u", v->slot_index);
                if (ImGui::Selectable(sl, is_sel)) {
                    selected_vehicle_slot_ = is_sel ? -1 : static_cast<int>(v->slot_index);
                }

                ImGui::TableSetColumnIndex(1);
                if (v->is_local_vehicle) ImGui::TextColored(pal.success, "%u [ВЫ]", v->vehicle_id);
                else                     ImGui::Text("%u", v->vehicle_id);

                ImGui::TableSetColumnIndex(2);
                if (v->class_subtype == 11) ImGui::TextColored(pal.warning, "%u", v->class_subtype);
                else                        ImGui::Text("%u", v->class_subtype);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.0f км/ч", v->speed_magnitude * 3.6f);

                ImGui::TableSetColumnIndex(4);
                if (v->game_vehicle_valid) ImGui::Text("%.1f м", v->distance_to_local);
                else                       ImGui::TextDisabled("-");

                ImGui::TableSetColumnIndex(5);
                if (v->game_vehicle_valid) ImGui::Text("%.1f, %.1f, %.1f", v->pos[0], v->pos[1], v->pos[2]);
                else                       ImGui::TextDisabled("CVehicle* == NULL");

                ImGui::TableSetColumnIndex(6);
                if (ImGui::SmallButton("Hex")) {
                    JumpToMemoryAddress(v->game_vehicle_ptr ? v->game_vehicle_ptr : v->net_vehicle_ptr,
                                        "Vehicle", false);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    if (has_sel) {
        if (side_card) ImGui::SameLine();
        const inspector::VehicleEntry* sel = nullptr;
        for (const auto& v : snapshot_.vehicles) {
            if (static_cast<int>(v.slot_index) == selected_vehicle_slot_) {
                sel = &v;
                break;
            }
        }
        if (ImGui::BeginChild("##VehicleDetailCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            ApplyTouchKineticScrolling();
            if (!sel) {
                ImGui::TextDisabled("Слот авто #%d освобождён.", selected_vehicle_slot_);
                if (ImGui::SmallButton("Закрыть")) selected_vehicle_slot_ = -1;
            } else {
                ImGui::TextColored(pal.accent, "Слот #%u | VehID %u (подтип %u)",
                                   sel->slot_index, sel->vehicle_id, sel->class_subtype);
                ImGui::SameLine();
                if (ImGui::SmallButton("Закрыть")) selected_vehicle_slot_ = -1;
                ImGui::Separator();

                ImGui::Text("CNetVehicle*: 0x%" PRIXPTR, sel->net_vehicle_ptr);
                ImGui::Text("CVehicle*:    0x%" PRIXPTR, sel->game_vehicle_ptr);
                ImGui::Text("BackRef (+0xB8): 0x%" PRIXPTR " | +0x790: %.2f",
                            sel->back_ref_b8, sel->special_float_790);
                ImGui::Text("Скорость: %.2f м/с (%.1f км/ч)", sel->speed_magnitude, sel->speed_magnitude * 3.6f);
                ImGui::Text("MoveSpeed (+0x180): (%.3f, %.3f, %.3f)",
                            sel->move_speed[0], sel->move_speed[1], sel->move_speed[2]);
                ImGui::Text("TurnSpeed (+0x18C): (%.3f, %.3f, %.3f)",
                            sel->turn_speed[0], sel->turn_speed[1], sel->turn_speed[2]);

                if (sel->game_vehicle_ptr != 0 && ImGui::SmallButton("CVehicle -> Hex")) {
                    JumpToMemoryAddress(sel->game_vehicle_ptr, "CVehicle", false);
                }
                ImGui::SameLine();
                if (sel->game_vehicle_ptr != 0 && ImGui::SmallButton("+ Watch Speed")) {
                    AddWatchAddress(sel->game_vehicle_ptr + offsets::vehicle::kVehMoveSpeedOffset,
                                    WatchValueType::Vec3f, "Veh MoveSpeed");
                }
            }
        }
        ImGui::EndChild();
    }
}

void InspectorMenu::RenderSectionNetwork(uintptr_t module_base, float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);
    const auto& rc = snapshot_.rakclient;

    // Шапка RakClient и управления Read-Only монитором
    detail::DrawStatusPill(rc.is_connected ? "RAKNET ONLINE" : "RAKNET OFFLINE", rc.is_connected, pal);
    ImGui::SameLine();
    ImGui::Text("Ping: %d мс (avg %d / min %d) | MTU %d | %.1f пкт/с, %.1f RPC/с",
                rc.last_ping, rc.average_ping, rc.lowest_ping, rc.mtu_size,
                rate_pkts_per_sec_, rate_rpcs_per_sec_);

    const bool obs_on = net::NetworkCatalogAndObserver::IsObserverInstalled();
    if (!obs_on) {
        if (ImGui::Button("Включить пассивный Read-Only монитор Send/RPC (0x01A513B8 / 0x01A51AC8)") && module_base != 0) {
            net::NetworkCatalogAndObserver::InstallPassiveObserver(module_base);
        }
    } else {
        detail::DrawStatusPill("МОНИТОР АКТИВЕН", true, pal);
    }
    ImGui::SameLine();
    if (detail::DrawFilterChip("Live-монитор и Byte-Diff", net_subtab_ == 0, pal)) net_subtab_ = 0;
    ImGui::SameLine();
    if (detail::DrawFilterChip("Raw-пакеты (11 ID)", net_subtab_ == 1, pal)) net_subtab_ = 1;
    ImGui::SameLine();
    if (detail::DrawFilterChip("Каталог RPC (26 ID)", net_subtab_ == 2, pal)) net_subtab_ = 2;

    ImGui::Separator();

    std::vector<net::CapturedNetEvent> events;
    std::vector<net::CounterSnapshotEntry> pkt_counters, rpc_counters;
    uint64_t total_pkts = 0, total_rpcs = 0;
    net::NetworkCatalogAndObserver::GetCapturedSnapshot(events, pkt_counters, rpc_counters, total_pkts, total_rpcs);

    if (net_subtab_ == 0) {
        bool paused = net::NetworkCatalogAndObserver::IsCapturePaused();
        if (ImGui::Checkbox("Пауза захвата", &paused)) {
            net::NetworkCatalogAndObserver::SetCapturePaused(paused);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Подсветка изменившихся байт (Byte-Diff)", &highlight_byte_diff_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Очистить буфер")) {
            net::NetworkCatalogAndObserver::ClearCapturedEvents();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Экспорт .CSV")) {
            ExportNetworkEventsCsv();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f * ui_scale);
        ImGui::InputInt("Фильтр ID (-1=все)", &log_filter_id_);

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const bool split_h  = (avail_w >= 680.0f * ui_scale);
        const float list_w  = split_h ? (avail_w * 0.50f) : 0.0f;
        const float list_h  = split_h ? 0.0f : (ImGui::GetContentRegionAvail().y * 0.48f);

        if (ImGui::BeginTable("##NetLiveStreamTbl", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(list_w, list_h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 52.0f * ui_scale);
            ImGui::TableSetupColumn("Тип",    ImGuiTableColumnFlags_WidthFixed, 44.0f * ui_scale);
            ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed, 48.0f * ui_scale);
            ImGui::TableSetupColumn("Байт",   ImGuiTableColumnFlags_WidthFixed, 50.0f * ui_scale);
            ImGui::TableSetupColumn("Caller VA");
            ImGui::TableSetupColumn("Назначение");
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                const auto& ev = *it;
                if (log_filter_id_ >= 0 && ev.id != log_filter_id_) continue;
                const bool is_sel = (selected_event_seq_ == ev.seq_no);

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(ev.seq_no & 0x7FFFFFFFU));

                ImGui::TableSetColumnIndex(0);
                char sq[24]{};
                std::snprintf(sq, sizeof(sq), "%" PRIu64, ev.seq_no);
                if (ImGui::Selectable(sq, is_sel, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_event_seq_ = ev.seq_no;
                }

                ImGui::TableSetColumnIndex(1);
                if (ev.kind == net::NetEventKind::RawPacket) ImGui::TextColored(pal.accent, "RAW");
                else                                         ImGui::TextColored(pal.warning, "RPC");

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", ev.id);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", ev.total_bytes);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("0x%07" PRIXPTR, ev.caller_va);

                ImGui::TableSetColumnIndex(5);
                if (ev.kind == net::NetEventKind::RawPacket) {
                    const auto* pi = net::NetworkCatalogAndObserver::FindPacketInfo(static_cast<uint8_t>(ev.id));
                    ImGui::TextDisabled("%s", pi ? pi->role_and_confidence : "Raw Packet");
                } else {
                    const auto* ri = net::NetworkCatalogAndObserver::FindRpcInfo(ev.id);
                    ImGui::TextDisabled("%s", ri ? ri->confidence_note : "RPC Call");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (split_h) ImGui::SameLine();

        if (ImGui::BeginChild("##NetBitStreamInspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            ApplyTouchKineticScrolling();
            const net::CapturedNetEvent* sel = nullptr;
            const net::CapturedNetEvent* prev_same_id = nullptr;
            for (size_t i = 0; i < events.size(); ++i) {
                if (events[i].seq_no == selected_event_seq_) {
                    sel = &events[i];
                    for (size_t j = i; j > 0; --j) {
                        const auto& cand = events[j - 1];
                        if (cand.kind == sel->kind && cand.id == sel->id) {
                            prev_same_id = &cand;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!sel && !events.empty()) {
                sel = &events.back();
            }

            if (!sel) {
                ImGui::TextDisabled("Буфер пуст — включите Read-Only монитор Send/RPC выше.");
            } else {
                ImGui::TextColored(pal.accent, "Кадр #%" PRIu64 " | %s ID %d (%d бит / %d Б)",
                                   sel->seq_no,
                                   (sel->kind == net::NetEventKind::RawPacket) ? "RAW ПАКЕТ" : "RPC",
                                   sel->id, sel->total_bits, sel->total_bytes);
                ImGui::Text("Priority: %d | Reliability: %d | Chan: %d | Caller VA: 0x%07" PRIXPTR,
                            sel->priority, sel->reliability, sel->channel, sel->caller_va);
                if (module_base != 0 && sel->caller_va != 0) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Caller -> ASM")) {
                        JumpToMemoryAddress(module_base + sel->caller_va, "Caller Send/RPC", true);
                    }
                }
                ImGui::Separator();

                if (sel->kind == net::NetEventKind::RawPacket && sel->captured_len > 0) {
                    const std::string dissected = net::NetworkCatalogAndObserver::DissectPacketPayload(
                        static_cast<uint8_t>(sel->id),
                        sel->preview_bytes.data(),
                        sel->captured_len,
                        sel->total_bits);
                    ImGui::TextWrapped("%s", dissected.c_str());
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Hex-дамп первых %zu байт (жёлтым отмечены байты, изменившиеся с прошлого ID %d):",
                                    sel->captured_len, sel->id);

                for (size_t row = 0; row < sel->captured_len; row += 16) {
                    ImGui::TextDisabled("+%02zX:", row);
                    for (size_t b = 0; b < 16 && (row + b) < sel->captured_len; ++b) {
                        ImGui::SameLine(0.0f, (b == 8) ? 10.0f * ui_scale : 4.0f * ui_scale);
                        const size_t idx = row + b;
                        const bool diff = highlight_byte_diff_ && prev_same_id &&
                                          idx < prev_same_id->captured_len &&
                                          prev_same_id->preview_bytes[idx] != sel->preview_bytes[idx];
                        if (diff) {
                            ImGui::TextColored(pal.warning, "%02X", sel->preview_bytes[idx]);
                        } else {
                            ImGui::Text("%02X", sel->preview_bytes[idx]);
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
    } else if (net_subtab_ == 1) {
        const auto& pkts = net::NetworkCatalogAndObserver::GetPacketCatalog();
        if (ImGui::BeginTable("##RawPktCatalog2", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID",          ImGuiTableColumnFlags_WidthFixed, 44.0f * ui_scale);
            ImGui::TableSetupColumn("Роль");
            ImGui::TableSetupColumn("Размер",      ImGuiTableColumnFlags_WidthFixed, 80.0f * ui_scale);
            ImGui::TableSetupColumn("Сериализатор",ImGuiTableColumnFlags_WidthFixed, 92.0f * ui_scale);
            ImGui::TableSetupColumn("Счётчик",     ImGuiTableColumnFlags_WidthFixed, 76.0f * ui_scale);
            ImGui::TableSetupColumn("Структура полей");
            ImGui::TableSetupColumn("Код",         ImGuiTableColumnFlags_WidthFixed, 54.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (const auto& p : pkts) {
                uint64_t cnt = 0;
                for (const auto& c : pkt_counters) {
                    if (c.id == p.packet_id) { cnt = c.count; break; }
                }
                ImGui::TableNextRow();
                ImGui::PushID(p.packet_id);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(pal.accent, "%u", p.packet_id);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(p.role_and_confidence);
                ImGui::TableSetColumnIndex(2);
                if (p.logical_size_bytes > 0) ImGui::Text("%d Б", p.logical_size_bytes);
                else                          ImGui::TextDisabled("перем.");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", p.serializer_va);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%" PRIu64, cnt);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextDisabled("%s", p.layout_summary);
                ImGui::TableSetColumnIndex(6);
                const uintptr_t sva = ParseHexVaString(p.serializer_va);
                if (sva != 0 && ImGui::SmallButton("ASM") && module_base != 0) {
                    JumpToMemoryAddress(module_base + sva, p.role_and_confidence, true);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::Checkbox("Только 18 строгих RPC кандидатов", &rpc_only_strict_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f * ui_scale);
        ImGui::InputText("Поиск RPC", rpc_search_buf_, sizeof(rpc_search_buf_));

        const auto& rpcs = net::NetworkCatalogAndObserver::GetRpcCatalog();
        if (ImGui::BeginTable("##RpcCatalog2", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("RPC ID",     ImGuiTableColumnFlags_WidthFixed, 64.0f * ui_scale);
            ImGui::TableSetupColumn("Класс",      ImGuiTableColumnFlags_WidthFixed, 74.0f * ui_scale);
            ImGui::TableSetupColumn("Примечание и подсистема");
            ImGui::TableSetupColumn("Примеры VA", ImGuiTableColumnFlags_WidthFixed, 120.0f * ui_scale);
            ImGui::TableSetupColumn("Вызовов",    ImGuiTableColumnFlags_WidthFixed, 76.0f * ui_scale);
            ImGui::TableSetupColumn("Код",        ImGuiTableColumnFlags_WidthFixed, 54.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (const auto& r : rpcs) {
                if (rpc_only_strict_ && !r.is_strict_candidate) continue;
                if (rpc_search_buf_[0] != '\0') {
                    char tmp[96]{};
                    std::snprintf(tmp, sizeof(tmp), "%d %s", r.rpc_id, r.confidence_note);
                    if (!detail::ContainsCaseInsensitive(tmp, rpc_search_buf_)) continue;
                }
                uint64_t cnt = 0;
                for (const auto& c : rpc_counters) {
                    if (c.id == r.rpc_id) { cnt = c.count; break; }
                }
                ImGui::TableNextRow();
                ImGui::PushID(r.rpc_id);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(pal.accent, "%d", r.rpc_id);
                ImGui::TableSetColumnIndex(1);
                if (r.is_strict_candidate) ImGui::TextColored(pal.success, "Строгий");
                else                       ImGui::TextDisabled("Широкий");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(r.confidence_note);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", r.sample_callsites_va);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%" PRIu64, cnt);
                ImGui::TableSetColumnIndex(5);
                const uintptr_t cva = ParseHexVaString(r.sample_callsites_va);
                if (cva != 0 && ImGui::SmallButton("ASM") && module_base != 0) {
                    JumpToMemoryAddress(module_base + cva, r.confidence_note, true);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

// ============================================================================
// Раздел 7 — MOD (наши функции поверх референсного инспектора)
// ============================================================================
void InspectorMenu::RenderSectionMod(float ui_scale) {
    const auto& lp = snapshot_.local_player;

    ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.25f, 1.0f), "MOD — действия (16.111.19050)");
    ImGui::SameLine();
    if (lp.valid) {
        char head[192]{};
        std::snprintf(head, sizeof(head),
                      "Игрок %u \"%s\" | HP-like=%.0f | pos=(%.1f, %.1f, %.1f)%s",
                      lp.player_id, lp.name.empty() ? "?" : lp.name.c_str(),
                      lp.ped_float_4b0, lp.pos[0], lp.pos[1], lp.pos[2],
                      lp.local_vehicle_ptr != 0 ? " | за рулём" : "");
        ImGui::TextDisabled("%s", head);
    } else {
        ImGui::TextDisabled("(локальный игрок не активен)");
    }

    if (mod_msg_ != 0) {
        ImGui::TextColored(mod_msg_ == 1 ? ImVec4(0.30f, 0.90f, 0.40f, 1.0f)
                                         : ImVec4(0.95f, 0.35f, 0.30f, 1.0f),
                           "%s", mod_msg_text_);
    }

    // --- Лечилка + Турбо ---------------------------------------------------
    if (ImGui::SmallButton("ЛЕЧИЛКА (HP/Armor = 100)")) {
        const auto r = br::mod::HealLocal();
        mod_msg_ = r.ok ? 1 : 2;
        std::snprintf(mod_msg_text_, sizeof(mod_msg_text_), "%s", r.msg);
    }
    if (lp.local_vehicle_ptr != 0) {
        ImGui::SameLine();
        const char* turbo_label = br::mod::TurboEnabled() ? "ТУРБО [ВКЛ] — выкл" : "ТУРБО [выкл] — вкл";
        if (ImGui::SmallButton(turbo_label)) {
            mod_turbo_on_ = !mod_turbo_on_;
            br::mod::SetTurbo(mod_turbo_on_, mod_turbo_mult_);
            mod_msg_ = 1;
            std::snprintf(mod_msg_text_, sizeof(mod_msg_text_),
                          "Турбо %s (x%.1f)", mod_turbo_on_ ? "включён" : "выключен", mod_turbo_mult_);
        }
        ImGui::SameLine();
        if (ImGui::SliderFloat("##ModTurboMult", &mod_turbo_mult_, 1.5f, 8.0f, "x%.1f")) {
            if (br::mod::TurboEnabled()) {
                br::mod::SetTurbo(true, mod_turbo_mult_);
            }
        }
    }

    // --- Телепорт -----------------------------------------------------------
    ImGui::SeparatorText("Телепорт");
    if (lp.ped_valid && ImGui::SmallButton("Тек. позиция")) {
        mod_tx_ = lp.pos[0];
        mod_ty_ = lp.pos[1];
        mod_tz_ = lp.pos[2];
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f * ui_scale);
    ImGui::InputFloat("X", &mod_tx_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f * ui_scale);
    ImGui::InputFloat("Y", &mod_ty_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f * ui_scale);
    ImGui::InputFloat("Z", &mod_tz_);
    ImGui::SameLine();
    if (ImGui::SmallButton("ТЕЛЕПОРТ") && lp.valid) {
        const auto r = br::mod::TeleportLocal(mod_tx_, mod_ty_, mod_tz_);
        mod_msg_ = r.ok ? 1 : 2;
        std::snprintf(mod_msg_text_, sizeof(mod_msg_text_), "%s", r.msg);
    }

    // --- Машины -------------------------------------------------------------
    ImGui::SeparatorText("Машины (VehiclePool) — «Въехать» / «Телепорт»");
    if (snapshot_.vehicles.empty()) {
        ImGui::TextDisabled("VehiclePool пуст (ещё не заполнен или чит не сработал)");
    } else {
        std::vector<const inspector::VehicleEntry*> vs;
        vs.reserve(snapshot_.vehicles.size());
        for (const auto& v : snapshot_.vehicles) {
            if (v.game_vehicle_valid) {
                vs.push_back(&v);
            }
        }
        std::sort(vs.begin(), vs.end(),
                  [](const inspector::VehicleEntry* a, const inspector::VehicleEntry* b) {
                      return a->distance_to_local < b->distance_to_local;
                  });
        const size_t max_rows = 60;
        if (vs.size() > max_rows) {
            vs.resize(max_rows);
        }

        if (ImGui::BeginTable("##ModVehicles", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Тип");
            ImGui::TableSetupColumn("Дист");
            ImGui::TableSetupColumn("Скор");
            ImGui::TableSetupColumn("Действие");
            ImGui::TableSetupColumn("Свой?");
            ImGui::TableHeadersRow();

            for (const auto* v : vs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%u", v->vehicle_id);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%u", v->class_subtype);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", v->distance_to_local);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", v->speed_magnitude);
                ImGui::TableSetColumnIndex(4);
                char id_in[48], id_tp[48];
                std::snprintf(id_in, sizeof(id_in), "Въехать##mvin%u", v->vehicle_id);
                std::snprintf(id_tp, sizeof(id_tp), "Телепорт##mvtp%u", v->vehicle_id);
                if (ImGui::SmallButton(id_in)) {
                    const auto r = br::mod::EnterVehicleById(v->vehicle_id);
                    mod_msg_ = r.ok ? 1 : 2;
                    std::snprintf(mod_msg_text_, sizeof(mod_msg_text_),
                                  "veh %u: %s", v->vehicle_id, r.msg);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(id_tp)) {
                    const auto r = br::mod::TeleportLocal(v->pos[0], v->pos[1], v->pos[2]);
                    mod_msg_ = r.ok ? 1 : 2;
                    std::snprintf(mod_msg_text_, sizeof(mod_msg_text_),
                                  "veh %u: %s", v->vehicle_id, r.msg);
                }
                ImGui::TableSetColumnIndex(5);
                if (v->is_local_vehicle) {
                    ImGui::TextColored(ImVec4(0.30f, 0.88f, 0.96f, 1.0f), "ваша");
                }
            }
            ImGui::EndTable();
        }
    }

    // --- Игроки ---------------------------------------------------------------
    ImGui::SeparatorText("Игроки (PlayerPool) — «Телепорт»");
    bool any_player = false;
    if (ImGui::BeginTable("##ModPlayers", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Ник");
        ImGui::TableSetupColumn("Дист");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Действие");
        ImGui::TableHeadersRow();

        for (const auto& p : snapshot_.remote_players) {
            if (!p.ped_valid) {
                continue;
            }
            any_player = true;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%u", p.reported_id);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(p.name.empty() ? "<пусто>" : p.name.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", p.distance_to_local);
            ImGui::TableSetColumnIndex(3); ImGui::Text("0x%08X", p.state_120);
            ImGui::TableSetColumnIndex(4);
            char id_ptp[48];
            std::snprintf(id_ptp, sizeof(id_ptp), "Телепорт##mp%u", p.reported_id);
            if (ImGui::SmallButton(id_ptp)) {
                const auto r = br::mod::TeleportLocal(p.pos[0], p.pos[1], p.pos[2]);
                mod_msg_ = r.ok ? 1 : 2;
                std::snprintf(mod_msg_text_, sizeof(mod_msg_text_),
                              "player %u: %s", p.reported_id, r.msg);
            }
        }
        ImGui::EndTable();
    }
    if (!any_player) {
        ImGui::TextDisabled("Дистанционные игроки не видны");
    }
}

} // namespace br::ui
