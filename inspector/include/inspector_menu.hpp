#pragma once

#include <cstdint>
#include <vector>

#include "game_reader.hpp"
#include "internal_imgui_bridge.hpp"
#include "network_catalog.hpp"

namespace br::ui {

struct InspectorMenuConfig {
    bool  menu_open               = true;
    bool  show_floating_pill      = true;
    bool  show_mini_hud           = false;
    int   snapshot_refresh_ms     = 150;  // интервал обновления снимка пулов
    bool  allow_rakclient_vcall   = true; // опрос константных геттеров пинга/MTU
};

class InspectorMenu {
public:
    InspectorMenu();

    // Основная точка отрисовки внутри активного кадра встроенного ImGui 1.90.9
    void Render(const InternalImGuiRuntimeTable& rt_table);

    // Позволяет передать готовый снимок извне (например в десктопном/юнит тесте)
    void SetExternalSnapshotForTest(const inspector::WorldDiagnosticSnapshot& snap);

    InspectorMenuConfig& GetConfig() noexcept { return config_; }
    const InspectorMenuConfig& GetConfig() const noexcept { return config_; }

    // Для проверки рендера конкретной вкладки в тестах (-1 = автоматический выбор пользователя)
    void ForceSelectTabForTest(int tab_index) noexcept { forced_tab_index_ = tab_index; }

private:
    void RefreshSnapshotIfNeeded(uintptr_t module_base);
    void JumpToMemoryAddress(uintptr_t addr, const char* label);

    void RenderFloatingPill(float ui_scale);
    void RenderMiniHud(float ui_scale);
    void RenderMainWindow(const InternalImGuiRuntimeTable& rt_table, float ui_scale);

    void RenderTabModuleAndImGui(const InternalImGuiRuntimeTable& rt_table);
    void RenderTabLocalPlayer();
    void RenderTabMod(); // ВКЛАДКА МОДА (наши действия: телепорт/лечилка/посадка/турбо)
    void RenderTabPlayerPool();
    void RenderTabVehiclePool();
    void RenderTabNetwork(uintptr_t module_base);
    void RenderTabMemoryAndVaMap(const InternalImGuiRuntimeTable& rt_table);

    InspectorMenuConfig                config_{};
    inspector::WorldDiagnosticSnapshot snapshot_{};
    bool                               use_external_snapshot_ = false;
    uint64_t                           last_snapshot_ms_      = 0;
    int                                forced_tab_index_      = -1;
    bool                               switch_to_mem_tab_     = false;

    // Фильтры PlayerPool
    char player_search_buf_[64]        = {};
    bool filter_players_with_ped_      = false;
    bool filter_players_in_veh_        = false;
    int  selected_remote_slot_         = -1;

    // Фильтры VehiclePool
    char vehicle_search_buf_[32]      = {};
    bool filter_vehicles_with_gameptr_ = false;
    bool sort_vehicles_by_dist_        = true;

    // MOD-таб (наши функции)
    float mod_tx_   = 0.0f;
    float mod_ty_   = 0.0f;
    float mod_tz_   = 0.0f;
    bool  mod_use_current_pos_ = true;
    bool  mod_turbo_on_        = false;
    float mod_turbo_mult_      = 3.0f;
    int   mod_msg_ = 0; // 0=нет, 1=OK, 2=ошибка
    char  mod_msg_text_[160]   = {};

    // Фильтры Сети
    bool rpc_only_strict_              = false;
    char rpc_search_buf_[32]           = {};
    int  log_filter_kind_              = 0; // 0=Все, 1=Raw Packet, 2=RPC
    int  log_filter_id_                = -1;
    uint64_t selected_event_seq_       = 0;

    // Инспектор памяти (Read-Only)
    char      mem_addr_input_[32]      = {};
    uintptr_t mem_inspect_addr_        = 0;
    char      mem_inspect_label_[96]   = "Не выбран";
    size_t    mem_inspect_bytes_       = 256;
    size_t    mem_selected_offset_     = 0;
};

} // namespace br::ui
