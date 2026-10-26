#include "inspector_menu.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"
#include "arm64_hook.hpp"
#include "safe_memory.hpp"
#include "mod_features.hpp"

namespace br::ui {

namespace {

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

// Строго изолированная RAII-тема: не модифицирует глобальный GImGui->Style клиента навсегда
class ScopedInspectorTheme {
public:
    explicit ScopedInspectorTheme(float scale) {
        const float s = std::clamp(scale, 0.85f, 2.25f);

        PushVar(ImGuiStyleVar_WindowRounding,    10.0f * s);
        PushVar(ImGuiStyleVar_ChildRounding,     8.0f * s);
        PushVar(ImGuiStyleVar_FrameRounding,     6.0f * s);
        PushVar(ImGuiStyleVar_PopupRounding,     8.0f * s);
        PushVar(ImGuiStyleVar_ScrollbarRounding, 8.0f * s);
        PushVar(ImGuiStyleVar_GrabRounding,      6.0f * s);
        PushVar(ImGuiStyleVar_TabRounding,       6.0f * s);
        PushVar(ImGuiStyleVar_WindowBorderSize,  1.0f);
        PushVar(ImGuiStyleVar_FramePadding,      ImVec2(10.0f * s, 7.0f * s));
        PushVar(ImGuiStyleVar_ItemSpacing,       ImVec2(9.0f * s, 7.0f * s));
        PushVar(ImGuiStyleVar_ScrollbarSize,     16.0f * s);

        PushCol(ImGuiCol_WindowBg,             ImVec4(0.06f, 0.08f, 0.11f, 0.95f));
        PushCol(ImGuiCol_ChildBg,              ImVec4(0.09f, 0.11f, 0.15f, 0.90f));
        PushCol(ImGuiCol_PopupBg,              ImVec4(0.08f, 0.10f, 0.14f, 0.98f));
        PushCol(ImGuiCol_Border,               ImVec4(0.18f, 0.25f, 0.34f, 0.80f));
        PushCol(ImGuiCol_FrameBg,              ImVec4(0.12f, 0.16f, 0.22f, 1.00f));
        PushCol(ImGuiCol_FrameBgHovered,       ImVec4(0.17f, 0.23f, 0.32f, 1.00f));
        PushCol(ImGuiCol_FrameBgActive,        ImVec4(0.20f, 0.29f, 0.40f, 1.00f));
        PushCol(ImGuiCol_TitleBg,              ImVec4(0.05f, 0.07f, 0.10f, 1.00f));
        PushCol(ImGuiCol_TitleBgActive,        ImVec4(0.07f, 0.12f, 0.19f, 1.00f));
        PushCol(ImGuiCol_CheckMark,            ImVec4(0.22f, 0.78f, 0.95f, 1.00f));
        PushCol(ImGuiCol_SliderGrab,           ImVec4(0.18f, 0.66f, 0.86f, 1.00f));
        PushCol(ImGuiCol_SliderGrabActive,     ImVec4(0.26f, 0.80f, 0.98f, 1.00f));
        PushCol(ImGuiCol_Button,               ImVec4(0.14f, 0.24f, 0.36f, 1.00f));
        PushCol(ImGuiCol_ButtonHovered,        ImVec4(0.19f, 0.34f, 0.50f, 1.00f));
        PushCol(ImGuiCol_ButtonActive,         ImVec4(0.12f, 0.45f, 0.66f, 1.00f));
        PushCol(ImGuiCol_Header,               ImVec4(0.14f, 0.25f, 0.37f, 0.85f));
        PushCol(ImGuiCol_HeaderHovered,        ImVec4(0.19f, 0.34f, 0.50f, 0.95f));
        PushCol(ImGuiCol_HeaderActive,         ImVec4(0.22f, 0.42f, 0.62f, 1.00f));
        PushCol(ImGuiCol_Tab,                  ImVec4(0.10f, 0.15f, 0.22f, 1.00f));
        PushCol(ImGuiCol_TabHovered,           ImVec4(0.18f, 0.34f, 0.50f, 1.00f));
        PushCol(ImGuiCol_TabSelected,          ImVec4(0.14f, 0.40f, 0.60f, 1.00f));
        PushCol(ImGuiCol_TableHeaderBg,        ImVec4(0.11f, 0.16f, 0.23f, 1.00f));
        PushCol(ImGuiCol_TableRowBg,           ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        PushCol(ImGuiCol_TableRowBgAlt,        ImVec4(1.00f, 1.00f, 1.00f, 0.03f));
    }

    ~ScopedInspectorTheme() {
        ImGui::PopStyleColor(color_count_);
        ImGui::PopStyleVar(var_count_);
    }

    ScopedInspectorTheme(const ScopedInspectorTheme&) = delete;
    ScopedInspectorTheme& operator=(const ScopedInspectorTheme&) = delete;

private:
    void PushCol(ImGuiCol idx, const ImVec4& col) {
        ImGui::PushStyleColor(idx, col);
        ++color_count_;
    }
    void PushVar(ImGuiStyleVar idx, float val) {
        ImGui::PushStyleVar(idx, val);
        ++var_count_;
    }
    void PushVar(ImGuiStyleVar idx, const ImVec2& val) {
        ImGui::PushStyleVar(idx, val);
        ++var_count_;
    }

    int color_count_ = 0;
    int var_count_   = 0;
};

void DrawStatusBadge(const char* label, bool ok) {
    const ImVec4 col = ok ? ImVec4(0.20f, 0.86f, 0.50f, 1.0f)
                          : ImVec4(0.95f, 0.40f, 0.36f, 1.0f);
    ImGui::TextColored(col, "[%s]", label);
}

void DrawKeyValueAddrRow(const char* name,
                         uintptr_t addr,
                         uintptr_t module_base,
                         const char* note) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);

    ImGui::TableSetColumnIndex(1);
    if (addr != 0) {
        ImGui::Text("0x%012" PRIXPTR, addr);
    } else {
        ImGui::TextDisabled("NULL (0x0)");
    }

    ImGui::TableSetColumnIndex(2);
    if (module_base != 0 && addr >= module_base && addr < module_base + 0x05000000ULL) {
        ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.98f, 1.0f), "+0x%07" PRIXPTR, addr - module_base);
    } else {
        ImGui::TextDisabled("heap / ext");
    }

    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(note ? note : "");
}

} // namespace

InspectorMenu::InspectorMenu() {
    std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x0");
}

void InspectorMenu::SetExternalSnapshotForTest(const inspector::WorldDiagnosticSnapshot& snap) {
    snapshot_              = snap;
    use_external_snapshot_ = true;
}

void InspectorMenu::JumpToMemoryAddress(uintptr_t addr, const char* label) {
    mem_inspect_addr_    = addr;
    mem_selected_offset_ = 0;
    std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x%" PRIXPTR, addr);
    if (label && label != mem_inspect_label_) {
        std::snprintf(mem_inspect_label_, sizeof(mem_inspect_label_), "%s", label);
    }
    switch_to_mem_tab_ = true;
}

void InspectorMenu::RefreshSnapshotIfNeeded(uintptr_t module_base) {
    if (use_external_snapshot_) {
        return;
    }
    const uint64_t now = MonotonicNowMs();
    const uint64_t interval = static_cast<uint64_t>(std::max(16, config_.snapshot_refresh_ms));
    if (last_snapshot_ms_ == 0 || (now - last_snapshot_ms_) >= interval) {
        inspector::GameReader::CaptureSnapshot(module_base, snapshot_, config_.allow_rakclient_vcall);
        last_snapshot_ms_ = now;
    }
}

void InspectorMenu::Render(const InternalImGuiRuntimeTable& rt_table) {
    ImGuiIO& io = ImGui::GetIO();
    const float disp_h = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 1080.0f;
    const float ui_scale = std::clamp(disp_h / 1080.0f, 0.85f, 2.0f);

    RefreshSnapshotIfNeeded(rt_table.module_base);

    // Турбо-тик мода (каждый кадр, GL-поток)
    br::mod::Tick();

    ScopedInspectorTheme theme(ui_scale);

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
    ImGui::SetNextWindowPos(ImVec2(24.0f * ui_scale, 24.0f * ui_scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##BRInspectorFloatingPill", nullptr, flags)) {
        const ImVec4 accent = config_.menu_open
            ? ImVec4(0.22f, 0.84f, 0.96f, 1.0f)
            : ImVec4(0.65f, 0.75f, 0.85f, 1.0f);

        ImGui::TextColored(accent, "BR 16.111");
        ImGui::SameLine();
        if (ImGui::Button(config_.menu_open ? "Скрыть UI" : "Инспектор")) {
            config_.menu_open = !config_.menu_open;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(config_.show_mini_hud ? "HUD:ВКЛ" : "HUD")) {
            config_.show_mini_hud = !config_.show_mini_hud;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %.0f FPS | P:%zu V:%zu",
                            ImGui::GetIO().Framerate,
                            snapshot_.remote_players.size(),
                            snapshot_.vehicles.size());
    }
    ImGui::End();
}

void InspectorMenu::RenderMiniHud(float ui_scale) {
    ImGui::SetNextWindowPos(ImVec2(24.0f * ui_scale, 84.0f * ui_scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##BRInspectorMiniHud", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.98f, 1.0f),
                           "Телеметрия 16.111.19050 (Read-Only)");
        ImGui::Separator();

        const auto& lp = snapshot_.local_player;
        if (lp.valid) {
            ImGui::Text("LocalPlayer: ID %u (%s)",
                        lp.player_id,
                        lp.name.empty() ? "-" : lp.name.c_str());
            if (lp.ped_valid) {
                ImGui::Text("XYZ: %.2f, %.2f, %.2f", lp.pos[0], lp.pos[1], lp.pos[2]);
                ImGui::Text("CPed +4B0/+4B8: %.1f / %.1f | InVeh: %u",
                            lp.ped_float_4b0, lp.ped_float_4b8, lp.ped_in_vehicle_flag);
            }
            if (lp.local_vehicle_ptr != 0) {
                ImGui::Text("Авто ID: %u (подтип %u)", lp.local_vehicle_id, lp.local_vehicle_subtype);
            }
        } else {
            ImGui::TextDisabled("LocalPlayer не инициализирован");
        }

        ImGui::Text("Пулы: Игроки %zu/1504 | Авто %zu/2000 | Объекты %zu/4096",
                    snapshot_.remote_players.size(),
                    snapshot_.vehicles.size(),
                    snapshot_.active_object_slots);

        if (snapshot_.rakclient.queried_live_methods && snapshot_.rakclient.is_connected) {
            ImGui::Text("RakClient: Ping %d мс (avg %d) | MTU %d",
                        snapshot_.rakclient.last_ping,
                        snapshot_.rakclient.average_ping,
                        snapshot_.rakclient.mtu_size);
        }
    }
    ImGui::End();
}

void InspectorMenu::RenderMainWindow(const InternalImGuiRuntimeTable& rt_table, float ui_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const float def_w = std::min(980.0f * ui_scale, std::max(540.0f, io.DisplaySize.x * 0.88f));
    const float def_h = std::min(640.0f * ui_scale, std::max(380.0f, io.DisplaySize.y * 0.82f));

    ImGui::SetNextWindowSize(ImVec2(def_w, def_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60.0f * ui_scale, 90.0f * ui_scale), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("BLACK RUSSIA 16.111.19050 — Инспектор Памяти, Пулов и Сети (Read-Only)###BRInspectorMain",
                      &config_.menu_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    // Верхняя инфо-полоса статуса
    DrawStatusBadge(rt_table.build_id_verified ? "ELF Build-ID OK" : "Build-ID ОТЛИЧАЕТСЯ",
                    rt_table.build_id_verified);
    ImGui::SameLine();
    ImGui::Text("Base: 0x%" PRIXPTR " | Собственный ImGui %s (OwnCtx: 0x%" PRIXPTR ")",
                rt_table.module_base,
                IMGUI_VERSION,
                reinterpret_cast<uintptr_t>(ImGui::GetCurrentContext()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f * ui_scale);
    ImGui::SliderInt("Опрос (мс)", &config_.snapshot_refresh_ms, 30, 1000);
    ImGui::Separator();

    if (ImGui::BeginTabBar("##BRInspectorMainTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const auto tab_flags_for = [&](int idx, bool force_mem = false) -> ImGuiTabItemFlags {
            if (forced_tab_index_ == idx) {
                return ImGuiTabItemFlags_SetSelected;
            }
            if (force_mem && switch_to_mem_tab_) {
                return ImGuiTabItemFlags_SetSelected;
            }
            return ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("0. MOD (действия)", nullptr, tab_flags_for(6))) {
            RenderTabMod();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("1. Модуль и ImGui", nullptr, tab_flags_for(0))) {
            RenderTabModuleAndImGui(rt_table);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("2. LocalPlayer и CPed", nullptr, tab_flags_for(1))) {
            RenderTabLocalPlayer();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("3. PlayerPool (1504)", nullptr, tab_flags_for(2))) {
            RenderTabPlayerPool();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("4. VehiclePool (2000)", nullptr, tab_flags_for(3))) {
            RenderTabVehiclePool();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("5. Сеть: Пакеты и RPC", nullptr, tab_flags_for(4))) {
            RenderTabNetwork(rt_table.module_base);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("6. Память и Карта VA", nullptr, tab_flags_for(5, true))) {
            switch_to_mem_tab_ = false;
            RenderTabMemoryAndVaMap(rt_table);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void InspectorMenu::RenderTabMod() {
    const auto& lp = snapshot_.local_player;

    ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.25f, 1.0f), "MOD — действия (16.111.19050)");
    if (lp.valid) {
        char head[192]{};
        std::snprintf(head, sizeof(head),
                      "Игрок %u \"%s\" | HP-like=%.0f | pos=(%.1f, %.1f, %.1f)%s",
                      lp.player_id, lp.name.empty() ? "?" : lp.name.c_str(),
                      lp.ped_float_4b0, lp.pos[0], lp.pos[1], lp.pos[2],
                      lp.local_vehicle_ptr != 0 ? " | за рулём" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", head);
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("(локальный игрок не активен)");
    }

    if (mod_msg_ != 0) {
        ImGui::TextColored(mod_msg_ == 1 ? ImVec4(0.30f, 0.90f, 0.40f, 1.0f)
                                         : ImVec4(0.95f, 0.35f, 0.30f, 1.0f),
                           "%s", mod_msg_text_);
    }

    // --- Лечилка + Турбо -------------------------------------------------
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

    // --- Телепорт ---------------------------------------------------------
    ImGui::SeparatorText("Телепорт");
    if (lp.ped_valid) {
        if (ImGui::SmallButton("Тек. позиция")) {
            mod_tx_ = lp.pos[0];
            mod_ty_ = lp.pos[1];
            mod_tz_ = lp.pos[2];
        }
        ImGui::SameLine();
    }
    ImGui::InputFloat("X", &mod_tx_);
    ImGui::SameLine();
    ImGui::InputFloat("Y", &mod_ty_);
    ImGui::SameLine();
    ImGui::InputFloat("Z", &mod_tz_);
    ImGui::SameLine();
    if (ImGui::SmallButton("ТЕЛЕПОРТ") && lp.valid) {
        const auto r = br::mod::TeleportLocal(mod_tx_, mod_ty_, mod_tz_);
        mod_msg_ = r.ok ? 1 : 2;
        std::snprintf(mod_msg_text_, sizeof(mod_msg_text_), "%s", r.msg);
    }

    // --- Машины -----------------------------------------------------------
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

    // --- Игроки ------------------------------------------------------------
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

void InspectorMenu::RenderTabModuleAndImGui(const InternalImGuiRuntimeTable& rt_table) {
    ImGui::TextColored(ImVec4(0.32f, 0.85f, 0.98f, 1.0f),
                       "Сводка бинаря %s — %s",
                       offsets::kTargetModuleName,
                       offsets::kTargetBuildVersion);

    const std::string obs_bid = InternalImGuiBridge::FormatBuildIdHex(rt_table.observed_build_id);
    const std::string exp_bid = InternalImGuiBridge::FormatBuildIdHex(offsets::kExpectedGnuBuildId);

    ImGui::BulletText("Эталонный SHA-256: %s", offsets::kTargetSha256);
    ImGui::BulletText("GNU Build-ID (.note +0x2E0): %s (ожидался %s)",
                      obs_bid.c_str(), exp_bid.c_str());
    ImGui::BulletText("Режим работы: Собственный изолированный рендер OpenGL ES 3.0 + Read-Only SafeMemory");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Собственный контекст Dear ImGui 1.90.9 (Standalone OpenGL ES 3.0)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiContext* ctx = ImGui::GetCurrentContext();

        ImGui::Text("Own ImGuiContext*: 0x%" PRIXPTR " | Отрисовано кадров: %" PRIu64,
                    reinterpret_cast<uintptr_t>(ctx),
                    rt_table.rendered_frames_total);
        ImGui::Text("Хук кадра GLES3: %s",
                    rt_table.render_hook_name ? rt_table.render_hook_name : "-");
        ImGui::Text("Хук тачскрина:   %s",
                    rt_table.touch_hook_name ? rt_table.touch_hook_name : "-");
        ImGui::Text("Шрифт меню:      %s",
                    rt_table.loaded_font_label ? rt_table.loaded_font_label : "-");
        ImGui::Text("Поверхность: %.0f x %.0f | FPS: %.1f | Касаний: %" PRIu64 " (X=%.0f, Y=%.0f, %s)",
                    io.DisplaySize.x, io.DisplaySize.y, io.Framerate,
                    rt_table.touch_events_total,
                    rt_table.last_touch_x, rt_table.last_touch_y,
                    rt_table.last_touch_down ? "НАЖАТ" : "ОТПУЩЕН");

        uintptr_t client_gimgui = 0;
        if (rt_table.module_base != 0) {
            mem::SafeMemory::Read<uintptr_t>(
                rt_table.module_base + offsets::imgui_va::kGImGuiPtr, client_gimgui);
        }
        ImGui::TextDisabled("Справочно (клиентский GImGui @ VA 0x03C9D9D0): 0x%" PRIXPTR " (не используется нашим меню)",
                            client_gimgui);
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Глобальные указатели пулов (.bss 0x036130A0..0x03613100)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Разрешить константные vtable-геттеры пинга/MTU у RakClient",
                        &config_.allow_rakclient_vcall);

        if (ImGui::BeginTable("##GlobalPoolsTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Глобаль");
            ImGui::TableSetupColumn("Статический VA");
            ImGui::TableSetupColumn("Текущий указатель");
            ImGui::TableSetupColumn("Сводка");
            ImGui::TableSetupColumn("Память");
            ImGui::TableHeadersRow();

            struct PoolRow {
                const char* name;
                uintptr_t   static_va;
                uintptr_t   live_ptr;
                char        summary[64];
            };

            PoolRow rows[6]{};
            rows[0] = {"g_pRakClient", offsets::globals::kRakClientInterfacePtr, snapshot_.rakclient.rakclient_ptr, ""};
            std::snprintf(rows[0].summary, sizeof(rows[0].summary),
                          "vtable %s", snapshot_.rakclient.vtable_verified ? "0x2140BF0 OK" : "не совп./NULL");

            rows[1] = {"g_pPlayerPool", offsets::globals::kPlayerPoolPtr, snapshot_.player_pool_ptr, ""};
            std::snprintf(rows[1].summary, sizeof(rows[1].summary),
                          "Активных удалённых: %zu / 1504", snapshot_.remote_players.size());

            rows[2] = {"g_pVehiclePool", offsets::globals::kVehiclePoolPtr, snapshot_.vehicle_pool_ptr, ""};
            std::snprintf(rows[2].summary, sizeof(rows[2].summary),
                          "Активных слотов: %zu / 2000", snapshot_.vehicles.size());

            rows[3] = {"g_pObjectPool", offsets::globals::kObjectPoolPtr, snapshot_.object_pool_ptr, ""};
            std::snprintf(rows[3].summary, sizeof(rows[3].summary),
                          "Активных слотов: %zu / 4096", snapshot_.active_object_slots);

            rows[4] = {"g_pAuxPool_0C8", offsets::globals::kPickupOrAuxPoolPtr, snapshot_.pickup_aux_pool_ptr, "Вспом. пул (0x17DD60C)"};
            rows[5] = {"g_pAuxPool_0F8", offsets::globals::kTextLabelOrAuxPoolPtr, snapshot_.textlabel_aux_pool_ptr, "Пул меток/состояний (0x17899C0)"};

            for (int i = 0; i < 6; ++i) {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(rows[i].name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%07" PRIXPTR, rows[i].static_va);
                ImGui::TableSetColumnIndex(2);
                if (rows[i].live_ptr != 0) {
                    ImGui::Text("0x%012" PRIXPTR, rows[i].live_ptr);
                } else {
                    ImGui::TextDisabled("NULL");
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(rows[i].summary);
                ImGui::TableSetColumnIndex(4);
                ImGui::BeginDisabled(rows[i].live_ptr == 0);
                if (ImGui::SmallButton("Hex ->")) {
                    JumpToMemoryAddress(rows[i].live_ptr, rows[i].name);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

void InspectorMenu::RenderTabLocalPlayer() {
    const auto& lp = snapshot_.local_player;
    if (!lp.valid) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
                           "CLocalPlayer сейчас не активен (PlayerPool == NULL или игрок ещё не подключён).");
        return;
    }

    ImGui::TextColored(ImVec4(0.30f, 0.88f, 0.96f, 1.0f),
                       "CLocalPlayer [0x%" PRIXPTR "] — ID: %u | Никнейм: %s",
                       lp.local_player_ptr,
                       lp.player_id,
                       lp.name.empty() ? "<пусто>" : lp.name.c_str());

    if (ImGui::SmallButton("Открыть CLocalPlayer в Hex")) {
        JumpToMemoryAddress(lp.local_player_ptr, "CLocalPlayer");
    }
    if (lp.ped_ptr != 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Открыть CPed в Hex")) {
            JumpToMemoryAddress(lp.ped_ptr, "Local CPed");
        }
    }
    if (lp.local_vehicle_ptr != 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Открыть CVehicle в Hex")) {
            JumpToMemoryAddress(lp.local_vehicle_ptr, "Local CVehicle (+0x40)");
        }
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##LocalPlayerFields", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Объект / Смещение");
        ImGui::TableSetupColumn("Тип");
        ImGui::TableSetupColumn("Значение");
        ImGui::TableSetupColumn("Доказательство по 16.111.19050");
        ImGui::TableHeadersRow();

        auto row = [](const char* off, const char* type, const char* val, const char* ev) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(off);
            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", type);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(val);
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(ev);
        };

        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "%u (0x%04X)", lp.player_id, lp.player_id);
        row("LocalPlayer + 0x00", "u16", buf, "CLocalPlayer::GetID (0x178BB3C)");

        row("LocalPlayer + 0x02", "char[]", lp.name.c_str(), "CLocalPlayer::GetName (0x178BC0C)");

        std::snprintf(buf, sizeof(buf), "0x%" PRIXPTR, lp.ped_ptr);
        row("LocalPlayer + 0x38", "CPed*", buf, "CLocalPlayer::GetPlayerPed (0x178BB44), sender 30");

        std::snprintf(buf, sizeof(buf), "0x%" PRIXPTR " (vehID=%u, type=%u)",
                      lp.local_vehicle_ptr, lp.local_vehicle_id, lp.local_vehicle_subtype);
        row("LocalPlayer + 0x40", "CVehicle*", buf, "Sender 34 (0x178BDB0 -> veh+0x434)");

        if (lp.ped_valid) {
            std::snprintf(buf, sizeof(buf), "X=%.3f, Y=%.3f, Z=%.3f", lp.pos[0], lp.pos[1], lp.pos[2]);
            row("CPed + 0x38..0x40", "float[3]", buf, "Позиция XYZ (apply 0x17D4188 / sender 30 0x178EE04)");

            std::snprintf(buf, sizeof(buf), "0x%" PRIXPTR, lp.matrix_ptr);
            row("CPed + 0x48", "void*", buf, "Матрица трансформации в сериализаторе 30/31");

            std::snprintf(buf, sizeof(buf), "%.3f  (u16 wire: %u)",
                          lp.ped_float_4b0,
                          static_cast<uint16_t>(std::clamp(lp.ped_float_4b0, 0.0f, 65535.0f)));
            row("CPed + 0x4B0", "float", buf, "Конвертируется в u16 в пакетах 30 и 33 (HP-like)");

            std::snprintf(buf, sizeof(buf), "%.3f  (u16 wire: %u)",
                          lp.ped_float_4b8,
                          static_cast<uint16_t>(std::clamp(lp.ped_float_4b8, 0.0f, 65535.0f)));
            row("CPed + 0x4B8", "float", buf, "Конвертируется в u16 в пакетах 30 и 33 (Armour-like)");

            std::snprintf(buf, sizeof(buf), "0x%" PRIXPTR " | flag(+0x548) = %u",
                          lp.ped_vehicle_ptr, lp.ped_in_vehicle_flag);
            row("CPed + 0x540 / +0x548", "ptr / u8", buf, "Связь педа с транспортом (0x17D4188)");
        }

        std::snprintf(buf, sizeof(buf), "+1F4=0x%08X | +1F8=0x%08X | +200=0x%08X",
                      lp.sync_field_1f4, lp.sync_field_1f8, lp.sync_field_200);
        row("LocalPlayer + 0x1F4..0x200", "u32[3]", buf, "Источники синхронизации пакета 30 (0x178EE04)");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.65f, 0.82f, 0.95f, 1.0f),
                       "Кэш дифференциальной отправки пакета 33 (LocalPlayer + 0xF0..+0x10B, 28 Б):");
    char hex_line[160]{};
    size_t pos = 0;
    for (size_t i = 0; i < lp.cache_block_f0.size() && pos + 4 < sizeof(hex_line); ++i) {
        pos += static_cast<size_t>(
            std::snprintf(hex_line + pos, sizeof(hex_line) - pos, "%02X ", lp.cache_block_f0[i]));
    }
    ImGui::TextUnformatted(hex_line);
}

void InspectorMenu::RenderTabPlayerPool() {
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##PlayerSearch", "Поиск по нику или ID...",
                             player_search_buf_, sizeof(player_search_buf_));
    ImGui::SameLine();
    ImGui::Checkbox("Только с CPed*", &filter_players_with_ped_);
    ImGui::SameLine();
    ImGui::Checkbox("Только в авто (+0x548)", &filter_players_in_veh_);
    ImGui::SameLine();
    ImGui::TextDisabled("Активно: %zu / %zu (лимит ID %u)",
                        snapshot_.remote_players.size(),
                        offsets::player::kPlayerPoolSlotCount,
                        offsets::player::kMaxPlayerId);

    std::vector<const inspector::RemotePlayerEntry*> filtered;
    filtered.reserve(snapshot_.remote_players.size());

    int exact_id_query = -1;
    if (player_search_buf_[0] >= '0' && player_search_buf_[0] <= '9') {
        exact_id_query = std::atoi(player_search_buf_);
    }

    for (const auto& p : snapshot_.remote_players) {
        if (filter_players_with_ped_ && !p.ped_valid) continue;
        if (filter_players_in_veh_ && p.ped_in_vehicle == 0 && p.ped_vehicle_ptr == 0) continue;

        if (player_search_buf_[0] != '\0') {
            const bool id_match = (exact_id_query >= 0 &&
                                   (static_cast<int>(p.slot_id) == exact_id_query ||
                                    static_cast<int>(p.reported_id) == exact_id_query));
            if (!id_match && !ContainsCaseInsensitive(p.name, player_search_buf_)) {
                continue;
            }
        }
        filtered.push_back(&p);
    }

    const float table_h = (selected_remote_slot_ >= 0)
        ? ImGui::GetContentRegionAvail().y * 0.65f
        : ImGui::GetContentRegionAvail().y;

    const ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("##RemotePlayerPoolTable", 8, tflags, ImVec2(0.0f, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Слот", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Ник (+0x18)");
        ImGui::TableSetupColumn("Дист.", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Позиция XYZ (CPed+0x38)");
        ImGui::TableSetupColumn("+4B0 / +4B8");
        ImGui::TableSetupColumn("Авто (+548)");
        ImGui::TableSetupColumn("Aux A / B");
        ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int row_i = clipper.DisplayStart; row_i < clipper.DisplayEnd; ++row_i) {
                const auto* p = filtered[static_cast<size_t>(row_i)];
                ImGui::PushID(static_cast<int>(p->slot_id));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                const bool is_sel = (selected_remote_slot_ == static_cast<int>(p->slot_id));
                char id_lbl[24]{};
                std::snprintf(id_lbl, sizeof(id_lbl), "%u", p->slot_id);
                if (ImGui::Selectable(id_lbl, is_sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    selected_remote_slot_ = is_sel ? -1 : static_cast<int>(p->slot_id);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(p->name.empty() ? "<без имени>" : p->name.c_str());

                ImGui::TableSetColumnIndex(2);
                if (p->ped_valid) {
                    ImGui::Text("%.1f м", p->distance_to_local);
                } else {
                    ImGui::TextDisabled("вне стрима");
                }

                ImGui::TableSetColumnIndex(3);
                if (p->ped_valid) {
                    ImGui::Text("%.1f, %.1f, %.1f", p->pos[0], p->pos[1], p->pos[2]);
                } else {
                    ImGui::TextDisabled("CPed == NULL");
                }

                ImGui::TableSetColumnIndex(4);
                if (p->ped_valid) {
                    ImGui::Text("%.0f / %.0f", p->ped_float_4b0, p->ped_float_4b8);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(5);
                if (p->ped_in_vehicle || p->ped_vehicle_ptr != 0) {
                    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.60f, 1.0f), "В авто (%u)", p->ped_in_vehicle);
                } else {
                    ImGui::TextDisabled("Пешком");
                }

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%d / %d", p->pool_aux_a, p->pool_aux_b);

                ImGui::TableSetColumnIndex(7);
                if (ImGui::SmallButton("Hex")) {
                    char lbl[64]{};
                    std::snprintf(lbl, sizeof(lbl), "RemotePlayer[%u]", p->slot_id);
                    JumpToMemoryAddress(p->remote_ptr, lbl);
                }

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // Нижняя карточка выбранного слота
    if (selected_remote_slot_ >= 0) {
        const inspector::RemotePlayerEntry* sel = nullptr;
        for (const auto& p : snapshot_.remote_players) {
            if (static_cast<int>(p.slot_id) == selected_remote_slot_) {
                sel = &p;
                break;
            }
        }
        if (sel && ImGui::BeginChild("##SelectedRemoteDetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            ImGui::TextColored(ImVec4(0.30f, 0.86f, 0.98f, 1.0f),
                               "Детали CRemotePlayer [Слот %u, ID(+0x4A)=%u] — %s",
                               sel->slot_id, sel->reported_id, sel->name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Закрыть")) {
                selected_remote_slot_ = -1;
            }
            ImGui::Text("CRemotePlayer*: 0x%" PRIXPTR " (sizeof 0x288) | CPed*(+0x10): 0x%" PRIXPTR,
                        sel->remote_ptr, sel->ped_ptr);
            ImGui::Text("Поля состояния: +0x120=0x%08X | +0x150=0x%" PRIX64 " | +0x15B=0x%02X | +0x15C=%u",
                        sel->state_120, sel->field_150, sel->byte_15b, sel->u16_15c);
            if (sel->ped_ptr != 0 && ImGui::SmallButton("Инспектировать CPed этого игрока в Hex")) {
                char lbl[64]{};
                std::snprintf(lbl, sizeof(lbl), "CPed (Remote[%u])", sel->slot_id);
                JumpToMemoryAddress(sel->ped_ptr, lbl);
            }
            ImGui::EndChild();
        } else if (!sel) {
            selected_remote_slot_ = -1;
        }
    }
}

void InspectorMenu::RenderTabVehiclePool() {
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##VehSearch", "Фильтр по VehID или слоту...",
                             vehicle_search_buf_, sizeof(vehicle_search_buf_));
    ImGui::SameLine();
    ImGui::Checkbox("Только с CVehicle*", &filter_vehicles_with_gameptr_);
    ImGui::SameLine();
    ImGui::Checkbox("Сорт. по дистанции", &sort_vehicles_by_dist_);
    ImGui::SameLine();
    ImGui::TextDisabled("Активно: %zu / %zu (0x17DEE34)",
                        snapshot_.vehicles.size(),
                        offsets::vehicle::kVehiclePoolSlotCount);

    std::vector<const inspector::VehicleEntry*> filtered;
    filtered.reserve(snapshot_.vehicles.size());

    const int query_num = (vehicle_search_buf_[0] != '\0') ? std::atoi(vehicle_search_buf_) : -1;
    for (const auto& v : snapshot_.vehicles) {
        if (filter_vehicles_with_gameptr_ && !v.game_vehicle_valid) continue;
        if (vehicle_search_buf_[0] != '\0') {
            if (static_cast<int>(v.slot_index) != query_num &&
                static_cast<int>(v.vehicle_id) != query_num &&
                static_cast<int>(v.class_subtype) != query_num) {
                continue;
            }
        }
        filtered.push_back(&v);
    }

    if (sort_vehicles_by_dist_) {
        std::sort(filtered.begin(), filtered.end(),
                  [](const inspector::VehicleEntry* a, const inspector::VehicleEntry* b) {
                      if (a->is_local_vehicle != b->is_local_vehicle) {
                          return a->is_local_vehicle > b->is_local_vehicle;
                      }
                      if (a->game_vehicle_valid != b->game_vehicle_valid) {
                          return a->game_vehicle_valid > b->game_vehicle_valid;
                      }
                      return a->distance_to_local < b->distance_to_local;
                  });
    }

    const ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("##VehiclePoolTable", 9, tflags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Слот", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("VehID (+434)", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("Тип (+430)", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Дист.", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Скор.", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Позиция XYZ (+0x38)");
        ImGui::TableSetupColumn("CVehicle* / BackRef(+B8)");
        ImGui::TableSetupColumn("+0x790 (f32)", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableSetupColumn("Память", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto* v = filtered[static_cast<size_t>(i)];
                ImGui::PushID(static_cast<int>(v->slot_index));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (v->is_local_vehicle) {
                    ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.55f, 1.0f), "*%u", v->slot_index);
                } else {
                    ImGui::Text("%u", v->slot_index);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", v->vehicle_id);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", v->class_subtype);

                ImGui::TableSetColumnIndex(3);
                if (v->game_vehicle_valid) {
                    ImGui::Text("%.1f м", v->distance_to_local);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.2f", v->speed_magnitude);

                ImGui::TableSetColumnIndex(5);
                if (v->game_vehicle_valid) {
                    ImGui::Text("%.1f, %.1f, %.1f", v->pos[0], v->pos[1], v->pos[2]);
                } else {
                    ImGui::TextDisabled("NULL CVehicle");
                }

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("0x%" PRIXPTR " / 0x%" PRIXPTR, v->game_vehicle_ptr, v->back_ref_b8);

                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%.2f", v->special_float_790);

                ImGui::TableSetColumnIndex(8);
                const uintptr_t target = v->game_vehicle_ptr ? v->game_vehicle_ptr : v->net_vehicle_ptr;
                if (ImGui::SmallButton("Hex")) {
                    char lbl[64]{};
                    std::snprintf(lbl, sizeof(lbl), "Vehicle[slot=%u, id=%u]", v->slot_index, v->vehicle_id);
                    JumpToMemoryAddress(target, lbl);
                }

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void InspectorMenu::RenderTabNetwork(uintptr_t module_base) {
    const auto& rc = snapshot_.rakclient;

    DrawStatusBadge(rc.vtable_verified ? "RakClient vtable 0x2140BF0" : "RakClient не активен",
                    rc.vtable_verified);
    ImGui::SameLine();
    if (rc.queried_live_methods) {
        ImGui::Text("Статус: %s | Ping: %d мс (ср. %d, мин. %d) | MTU: %d",
                    rc.is_connected ? "ПОДКЛЮЧЁН" : "ОТКЛЮЧЁН",
                    rc.last_ping, rc.average_ping, rc.lowest_ping, rc.mtu_size);
    } else {
        ImGui::Text("RakClient*: 0x%" PRIXPTR " (vtable: 0x%" PRIXPTR ")",
                    rc.rakclient_ptr, rc.vtable_ptr);
    }

    // Управление пассивным Read-Only наблюдателем Send/RPC
    const bool obs_installed = net::NetworkCatalogAndObserver::IsObserverInstalled();
    if (!obs_installed) {
        if (ImGui::Button("Включить пассивный Read-Only логгер Send/RPC (0x1A513B8 / 0x1A51AC8)")) {
            net::NetworkCatalogAndObserver::InstallPassiveObserver(module_base);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(не модифицирует и не отправляет пакеты)");
    } else {
        bool paused = net::NetworkCatalogAndObserver::IsCapturePaused();
        if (ImGui::Checkbox("Пауза записи", &paused)) {
            net::NetworkCatalogAndObserver::SetCapturePaused(paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Очистить буфер")) {
            net::NetworkCatalogAndObserver::ClearCapturedEvents();
        }
    }

    std::vector<net::CapturedNetEvent>     events;
    std::vector<net::CounterSnapshotEntry> pkt_counters;
    std::vector<net::CounterSnapshotEntry> rpc_counters;
    uint64_t total_pkts = 0;
    uint64_t total_rpcs = 0;
    net::NetworkCatalogAndObserver::GetCapturedSnapshot(
        events, pkt_counters, rpc_counters, total_pkts, total_rpcs);

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.98f, 1.0f),
                       "| Захвачено: Raw=%llu, RPC=%llu",
                       static_cast<unsigned long long>(total_pkts),
                       static_cast<unsigned long long>(total_rpcs));
    ImGui::Separator();

    if (ImGui::BeginTabBar("##NetworkSubTabs")) {
        // Подвкладка 1: Каталог Raw BitStream пакетов (ID 27..39)
        if (ImGui::BeginTabItem("Каталог Raw-пакетов (11 ID / 14 Send)")) {
            ImGui::TextDisabled("Примечание анализа: 3 generic callback в 0x1933A7C (0x1933C5C/D50/D74) отсеяны как несетевые.");
            if (ImGui::BeginTable("##RawPacketCatalogTbl", 7,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("ID (Hex)", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableSetupColumn("Размер", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Счётчик", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Send VA");
                ImGui::TableSetupColumn("Serializer VA");
                ImGui::TableSetupColumn("Формат BitStream");
                ImGui::TableSetupColumn("Роль и уверенность");
                ImGui::TableHeadersRow();

                for (const auto& entry : net::NetworkCatalogAndObserver::GetPacketCatalog()) {
                    uint64_t hits = 0;
                    for (const auto& c : pkt_counters) {
                        if (c.id == static_cast<int>(entry.packet_id)) {
                            hits = c.count;
                            break;
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u (0x%02X)", entry.packet_id, entry.packet_id);
                    ImGui::TableSetColumnIndex(1);
                    if (entry.logical_size_bytes > 0) {
                        ImGui::Text("%d Б", entry.logical_size_bytes);
                    } else {
                        ImGui::TextDisabled("перем.");
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (hits > 0) {
                        ImGui::TextColored(ImVec4(0.30f, 0.92f, 0.55f, 1.0f), "%llu",
                                           static_cast<unsigned long long>(hits));
                    } else {
                        ImGui::TextDisabled("0");
                    }
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(entry.send_vas_csv);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(entry.serializer_va);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(entry.layout_summary);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(entry.role_and_confidence);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // Подвкладка 2: Каталог исходящих RPC (vtable +0xB0)
        if (ImGui::BeginTabItem("Каталог RPC (18 Strict / 26 Wide)")) {
            ImGui::Checkbox("Показать только строгую выборку (18 ID)", &rpc_only_strict_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##RpcFilter", "Поиск по RPC ID или VA...",
                                     rpc_search_buf_, sizeof(rpc_search_buf_));

            if (ImGui::BeginTable("##RpcCatalogTbl", 7,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("RPC ID", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Strict / Wide", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Функций", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Счётчик", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Уровень");
                ImGui::TableSetupColumn("Примеры Callsite VA");
                ImGui::TableSetupColumn("Примечание");
                ImGui::TableHeadersRow();

                for (const auto& r : net::NetworkCatalogAndObserver::GetRpcCatalog()) {
                    if (rpc_only_strict_ && !r.is_strict_candidate) continue;
                    if (rpc_search_buf_[0] != '\0') {
                        char id_str[32]{};
                        std::snprintf(id_str, sizeof(id_str), "%d 0x%x", r.rpc_id, r.rpc_id);
                        if (!ContainsCaseInsensitive(id_str, rpc_search_buf_) &&
                            !ContainsCaseInsensitive(r.sample_callsites_va, rpc_search_buf_)) {
                            continue;
                        }
                    }

                    uint64_t hits = 0;
                    for (const auto& c : rpc_counters) {
                        if (c.id == r.rpc_id) {
                            hits = c.count;
                            break;
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d (0x%X)", r.rpc_id, r.rpc_id);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d / %d", r.strict_callsite_count, r.wide_callsite_count);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", r.unique_func_count);
                    ImGui::TableSetColumnIndex(3);
                    if (hits > 0) {
                        ImGui::TextColored(ImVec4(0.30f, 0.92f, 0.55f, 1.0f), "%llu",
                                           static_cast<unsigned long long>(hits));
                    } else {
                        ImGui::TextDisabled("0");
                    }
                    ImGui::TableSetColumnIndex(4);
                    DrawStatusBadge(r.is_strict_candidate ? "STRICT" : "WIDE", r.is_strict_candidate);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(r.sample_callsites_va);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(r.confidence_note);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // Подвкладка 3: Живой кольцевой буфер захваченных BitStream кадров
        if (ImGui::BeginTabItem("Живой журнал BitStream (256)")) {
            ImGui::RadioButton("Все", &log_filter_kind_, 0); ImGui::SameLine();
            ImGui::RadioButton("Только Raw Send", &log_filter_kind_, 1); ImGui::SameLine();
            ImGui::RadioButton("Только RPC", &log_filter_kind_, 2); ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Фильтр ID (-1=все)", &log_filter_id_);

            const float list_h = ImGui::GetContentRegionAvail().y * 0.58f;
            const net::CapturedNetEvent* selected_ev = nullptr;

            if (ImGui::BeginTable("##LiveNetEventsTbl", 7,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0f, list_h))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableSetupColumn("Тип", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableSetupColumn("Размер", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableSetupColumn("Prio/Rel/Ch", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                ImGui::TableSetupColumn("Caller VA", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Декодированная сводка");
                ImGui::TableHeadersRow();

                for (const auto& ev : events) {
                    if (log_filter_kind_ == 1 && ev.kind != net::NetEventKind::RawPacket) continue;
                    if (log_filter_kind_ == 2 && ev.kind != net::NetEventKind::RpcCall) continue;
                    if (log_filter_id_ >= 0 && ev.id != log_filter_id_) continue;

                    ImGui::PushID(static_cast<int>(ev.seq_no));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    const bool is_sel = (selected_event_seq_ == ev.seq_no);
                    char seq_lbl[24]{};
                    std::snprintf(seq_lbl, sizeof(seq_lbl), "%llu",
                                  static_cast<unsigned long long>(ev.seq_no));
                    if (ImGui::Selectable(seq_lbl, is_sel, ImGuiSelectableFlags_SpanAllColumns)) {
                        selected_event_seq_ = ev.seq_no;
                    }
                    if (is_sel) {
                        selected_ev = &ev;
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (ev.kind == net::NetEventKind::RawPacket) {
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.98f, 1.0f), "SEND");
                    } else {
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.32f, 1.0f), "RPC");
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d (0x%X)", ev.id, ev.id);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d Б (%d б)", ev.total_bytes, ev.total_bits);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d / %d / %d", ev.priority, ev.reliability, ev.channel);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("0x%07" PRIXPTR, ev.caller_va);

                    ImGui::TableSetColumnIndex(6);
                    if (ev.kind == net::NetEventKind::RawPacket) {
                        const std::string summary = net::NetworkCatalogAndObserver::DissectPacketPayload(
                            static_cast<uint8_t>(ev.id),
                            ev.preview_bytes.data(),
                            ev.captured_len,
                            ev.total_bits);
                        ImGui::TextUnformatted(summary.c_str());
                    } else {
                        const auto* rinfo = net::NetworkCatalogAndObserver::FindRpcInfo(ev.id);
                        ImGui::Text("%s", rinfo ? rinfo->confidence_note : "Не из статической таблицы");
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginChild("##NetHexPreview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
                if (selected_ev) {
                    ImGui::TextColored(ImVec4(0.32f, 0.86f, 0.98f, 1.0f),
                                       "Кадр #%llu | %s ID=%d | %d Б (%d бит) | Caller VA: 0x%" PRIXPTR,
                                       static_cast<unsigned long long>(selected_ev->seq_no),
                                       selected_ev->kind == net::NetEventKind::RawPacket ? "Raw Packet" : "RPC",
                                       selected_ev->id,
                                       selected_ev->total_bytes,
                                       selected_ev->total_bits,
                                       selected_ev->caller_va);
                    char line[128]{};
                    for (size_t off = 0; off < selected_ev->captured_len; off += 16) {
                        size_t p = static_cast<size_t>(
                            std::snprintf(line, sizeof(line), "+%02zX: ", off));
                        for (size_t b = 0; b < 16 && (off + b) < selected_ev->captured_len; ++b) {
                            p += static_cast<size_t>(
                                std::snprintf(line + p, sizeof(line) - p, "%02X ",
                                              selected_ev->preview_bytes[off + b]));
                        }
                        ImGui::TextUnformatted(line);
                    }
                } else {
                    ImGui::TextDisabled("Выберите строку в таблице выше для просмотра Hex-дампа первых 96 байт BitStream.");
                }
                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void InspectorMenu::RenderTabMemoryAndVaMap(const InternalImGuiRuntimeTable& rt_table) {
    const uintptr_t base = rt_table.module_base;

    // Быстрые закладки на подтверждённые структуры и функции
    ImGui::TextColored(ImVec4(0.32f, 0.86f, 0.98f, 1.0f), "Быстрые закладки 16.111.19050:");
    if (base != 0) {
        if (ImGui::SmallButton("ELF Header (+0x0)")) JumpToMemoryAddress(base, "ELF Header & Build-ID");
        ImGui::SameLine();
        if (rt_table.p_gimgui && *rt_table.p_gimgui) {
            if (ImGui::SmallButton("GImGui Context")) {
                JumpToMemoryAddress(reinterpret_cast<uintptr_t>(*rt_table.p_gimgui), "ImGuiContext (0x62B8)");
            }
            ImGui::SameLine();
        }
        if (snapshot_.player_pool_ptr != 0) {
            if (ImGui::SmallButton("CPlayerPool")) JumpToMemoryAddress(snapshot_.player_pool_ptr, "CPlayerPool");
            ImGui::SameLine();
        }
        if (snapshot_.local_player.local_player_ptr != 0) {
            if (ImGui::SmallButton("CLocalPlayer")) {
                JumpToMemoryAddress(snapshot_.local_player.local_player_ptr, "CLocalPlayer");
            }
            ImGui::SameLine();
        }
        if (snapshot_.local_player.ped_ptr != 0) {
            if (ImGui::SmallButton("Local CPed")) {
                JumpToMemoryAddress(snapshot_.local_player.ped_ptr, "Local CPed");
            }
            ImGui::SameLine();
        }
        if (snapshot_.vehicle_pool_ptr != 0) {
            if (ImGui::SmallButton("CVehiclePool")) {
                JumpToMemoryAddress(snapshot_.vehicle_pool_ptr, "CVehiclePool");
            }
        }
    }

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Адрес (hex)", mem_addr_input_, sizeof(mem_addr_input_),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        mem_inspect_addr_ = static_cast<uintptr_t>(std::strtoull(mem_addr_input_, nullptr, 16));
        std::snprintf(mem_inspect_label_, sizeof(mem_inspect_label_), "Ручной ввод");
    }
    ImGui::SameLine();
    if (ImGui::Button("Перейти")) {
        mem_inspect_addr_ = static_cast<uintptr_t>(std::strtoull(mem_addr_input_, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("-0x100") && mem_inspect_addr_ >= 0x100) {
        JumpToMemoryAddress(mem_inspect_addr_ - 0x100, mem_inspect_label_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+0x100")) {
        JumpToMemoryAddress(mem_inspect_addr_ + 0x100, mem_inspect_label_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Объект: %s", mem_inspect_label_);

    const float hex_h = ImGui::GetContentRegionAvail().y * 0.54f;
    if (ImGui::BeginChild("##SafeHexViewer", ImVec2(0.0f, hex_h), ImGuiChildFlags_Border)) {
        if (mem_inspect_addr_ == 0) {
            ImGui::TextDisabled("Выберите закладку выше или введите hex-адрес для безопасного Read-Only чтения.");
        } else {
            std::vector<uint8_t> buf(mem_inspect_bytes_, 0);
            std::vector<uint8_t> ok_mask(mem_inspect_bytes_, 0);

            for (size_t off = 0; off < mem_inspect_bytes_; off += 16) {
                if (mem::SafeMemory::ReadBytes(mem_inspect_addr_ + off, &buf[off], 16)) {
                    std::memset(&ok_mask[off], 1, 16);
                } else {
                    for (size_t b = 0; b < 16; ++b) {
                        ok_mask[off + b] =
                            mem::SafeMemory::ReadBytes(mem_inspect_addr_ + off + b, &buf[off + b], 1) ? 1 : 0;
                    }
                }
            }

            // Инспектор значений по выбранному смещению
            if (mem_selected_offset_ + 8 <= buf.size() && ok_mask[mem_selected_offset_]) {
                uint16_t u16_v = 0;
                uint32_t u32_v = 0;
                uint64_t u64_v = 0;
                float    f32_v = 0.0f;
                std::memcpy(&u16_v, &buf[mem_selected_offset_], 2);
                std::memcpy(&u32_v, &buf[mem_selected_offset_], 4);
                std::memcpy(&u64_v, &buf[mem_selected_offset_], 8);
                std::memcpy(&f32_v, &buf[mem_selected_offset_], 4);

                ImGui::TextColored(ImVec4(0.40f, 0.92f, 0.65f, 1.0f),
                                   "[+0x%03zX] u8=%u | u16=%u (0x%04X) | u32=0x%08X | f32=%.5g | ptr=0x%012" PRIX64,
                                   mem_selected_offset_,
                                   buf[mem_selected_offset_],
                                   u16_v, u16_v, u32_v, f32_v, u64_v);
                if (mem::SafeMemory::IsPlausibleAddress(static_cast<uintptr_t>(u64_v))) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Перейти по ptr")) {
                        JumpToMemoryAddress(static_cast<uintptr_t>(u64_v), "Разыменованный ptr");
                    }
                }
                ImGui::Separator();
            }

            for (size_t row = 0; row < mem_inspect_bytes_; row += 16) {
                ImGui::PushID(static_cast<int>(row));
                ImGui::TextDisabled("+%03zX (0x%012" PRIXPTR "):", row, mem_inspect_addr_ + row);
                ImGui::SameLine();

                for (size_t b = 0; b < 16; ++b) {
                    const size_t idx = row + b;
                    char bstr[8]{};
                    if (ok_mask[idx]) {
                        std::snprintf(bstr, sizeof(bstr), "%02X", buf[idx]);
                    } else {
                        std::snprintf(bstr, sizeof(bstr), "??");
                    }
                    const bool sel = (mem_selected_offset_ == idx);
                    if (ImGui::Selectable(bstr, sel, ImGuiSelectableFlags_None, ImVec2(22.0f, 0.0f))) {
                        mem_selected_offset_ = idx;
                    }
                    ImGui::SameLine(0.0f, (b == 7) ? 10.0f : 3.0f);
                }

                char ascii[17]{};
                for (size_t b = 0; b < 16; ++b) {
                    const uint8_t c = buf[row + b];
                    ascii[b] = (ok_mask[row + b] && c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
                }
                ImGui::Text(" |%s|", ascii);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    // Справочник ключевых подтверждённых VA из анализа 16.111.19050
    if (ImGui::BeginTable("##VaReferenceTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Символ / Цепочка");
        ImGui::TableSetupColumn("Runtime адрес");
        ImGui::TableSetupColumn("VA в ELF");
        ImGui::TableSetupColumn("Описание");
        ImGui::TableHeadersRow();

        DrawKeyValueAddrRow("JNILib::step (GLThread)", base + offsets::jni_bridge::kJni_JNILib_step, base,
                            "Кадр рендера GLSurfaceView (точка отрисовки нашего ImGui)");
        DrawKeyValueAddrRow("JNILib::multiTouchEvent", base + offsets::jni_bridge::kJni_JNILib_multiTouchEvent, base,
                            "Точка приёма касаний экрана (action, count, x1, y1...)");
        DrawKeyValueAddrRow("ImGuiWrapper::Init", base + offsets::imgui_va::kWrapper_Init, base,
                            "DebugCheckVersion(1.90.9) + CreateContext + шрифты");
        DrawKeyValueAddrRow("ImGuiWrapper::BeginFrame", base + offsets::imgui_va::kWrapper_BeginFrame, base,
                            "ImGui_ImplOpenGL3_NewFrame + ImGui::NewFrame");
        DrawKeyValueAddrRow("ImGuiWrapper::EndAndRender", base + offsets::imgui_va::kWrapper_EndFrameAndRender, base,
                            "EndFrame + Render + ImGui_ImplOpenGL3_RenderDrawData");
        DrawKeyValueAddrRow("ImGui::NewFrame", base + offsets::imgui_va::kNewFrame, base,
                            "Ядро Dear ImGui 1.90.9 (19090)");
        DrawKeyValueAddrRow("ImGui_ImplOpenGL3_RenderDrawData", base + offsets::imgui_va::kImplOpenGL3_RenderDrawData, base,
                            "GLES3 рендер командных списков ImGui");
        DrawKeyValueAddrRow("PlayerPool::GetRemotePlayer", base + offsets::player::kFnPlayerPoolGetRemote, base,
                            "Проверка ID <= 1503, индексация base + 8 + id*8");
        DrawKeyValueAddrRow("VehiclePool::FindByGameVeh", base + offsets::vehicle::kFnVehiclePoolFindByGameVeh, base,
                            "Линейный обход 2000 слотов по 8 байт");
        DrawKeyValueAddrRow("Sender Packet 30 (73B)", base + 0x0178EE04ULL, base,
                            "0x178EE04 -> 0x17DB1EC -> Send 0x178F068");
        DrawKeyValueAddrRow("Sender Packet 34 (66B)", base + 0x0178BDB0ULL, base,
                            "0x178BDB0 -> 0x17DBAF0 -> Send 0x178C040 (VehicleSync)");
        DrawKeyValueAddrRow("Sender Packet 38 (32B)", base + 0x0178E5B4ULL, base,
                            "0x178E5B4 -> 0x17DC054 -> Send 0x178E844 (aimSync)");
        DrawKeyValueAddrRow("RakClient::Send Thunk", base + offsets::raknet::kFnRakClientSendThunkVA, base,
                            "vtable +0x38 (_ZThn1680_N9RakClient4Send...)");
        DrawKeyValueAddrRow("RakClient::RPC Thunk", base + offsets::raknet::kFnRakClientRpcThunkVA, base,
                            "vtable +0xB0 (_ZThn1680_N9RakClient3RPC...)");

        ImGui::EndTable();
    }
}

} // namespace br::ui
