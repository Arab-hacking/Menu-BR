#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game_reader.hpp"
#include "internal_imgui_bridge.hpp"
#include "network_catalog.hpp"

namespace br::ui {

enum class InspectorThemeId : int {
    CyberObsidian  = 0,
    CrimsonCarbon  = 1,
    EmeraldMatrix  = 2,
    DaylightFrost  = 3,
};

enum class WindowPresetId : int {
    StandardCenter = 0,
    CompactFloat   = 1,
    MaximizedSafe  = 2,
    SplitLeft      = 3,
    SplitRight     = 4,
};

enum class WatchValueType : int {
    Uint8   = 0,
    Uint16  = 1,
    Int32   = 2,
    Uint32  = 3,
    Float32 = 4,
    Ptr64   = 5,
    Vec3f   = 6,
    CString = 7,
};

struct MemoryWatchItem {
    uintptr_t      address    = 0;
    WatchValueType type       = WatchValueType::Float32;
    char           label[64]  = {};
    uint8_t        last_raw[24] = {};
    bool           has_prev   = false;
    uint64_t       changed_ms = 0;
};

struct MemoryScanHit {
    uintptr_t address = 0;
    uint8_t   preview[8] = {};
};

struct InspectorMenuConfig {
    bool             menu_open               = true;
    bool             show_floating_pill      = true;
    bool             show_mini_hud           = false;
    bool             pause_snapshots         = false;
    int              snapshot_refresh_ms     = 120;
    bool             allow_rakclient_vcall   = true;

    // Визуальная адаптация и удобство на сенсорном экране
    InspectorThemeId theme                   = InspectorThemeId::CyberObsidian;
    float            user_ui_scale           = 1.00f;
    float            window_bg_alpha         = 0.94f;
    float            safe_area_pad_x         = 20.0f;
    float            safe_area_pad_y         = 18.0f;
    bool             kinetic_touch_scroll    = true;
    bool             adaptive_sidebar        = true; // авто-переход боковой панели в верхнюю на узких окнах
};

class InspectorMenu {
public:
    InspectorMenu();

    // Основная точка отрисовки внутри собственного контекста Dear ImGui 1.90.9
    void Render(const InternalImGuiRuntimeTable& rt_table);

    // Позволяет передать готовый снимок извне (для модульных и headless-тестов)
    void SetExternalSnapshotForTest(const inspector::WorldDiagnosticSnapshot& snap);

    InspectorMenuConfig& GetConfig() noexcept { return config_; }
    const InspectorMenuConfig& GetConfig() const noexcept { return config_; }

    // Принудительный выбор раздела в тестах (-1 = выбор пользователя, 0..6 = разделы)
    void ForceSelectTabForTest(int tab_index) noexcept { forced_tab_index_ = tab_index; }

    void JumpToMemoryAddress(uintptr_t addr, const char* label, bool open_disasm = false);
    void AddWatchAddress(uintptr_t addr, WatchValueType type, const char* label);

private:
    static constexpr size_t kHistorySamples = 160;

    void RefreshSnapshotAndMetrics(uintptr_t module_base);
    void ApplyTouchKineticScrolling();
    void ApplyPendingWindowPreset(float ui_scale);

    // Экспорт Read-Only отчётов на Android / Linux
    bool ExportWorldSnapshotJson(const InternalImGuiRuntimeTable& rt_table);
    bool ExportNetworkEventsCsv();
    bool ExportMemoryViewTxt(uintptr_t module_base);

    // Основные поверхности
    void RenderFloatingPill(float ui_scale);
    void RenderMiniHud(float ui_scale);
    void RenderMainWindow(const InternalImGuiRuntimeTable& rt_table, float ui_scale);
    void RenderNavigationSidebar(float ui_scale, float sidebar_w);
    void RenderNavigationTopBar(float ui_scale);

    // 7 функциональных разделов
    void RenderSectionDashboard(const InternalImGuiRuntimeTable& rt_table, float ui_scale);
    void RenderSectionLocalPlayer(float ui_scale);
    void RenderSectionPlayerPool(float ui_scale);
    void RenderSectionVehiclePool(float ui_scale);
    void RenderSectionNetwork(uintptr_t module_base, float ui_scale);
    void RenderSectionMemoryLab(const InternalImGuiRuntimeTable& rt_table, float ui_scale);
    void RenderSectionSettingsAndExport(const InternalImGuiRuntimeTable& rt_table, float ui_scale);

    // 8-й раздел — MOD (наши функции: лечилка, турбо, телепорт, посадка)
    void RenderSectionMod(float ui_scale);

    // Состояние MOD-раздела
    float mod_tx_   = 0.0f;
    float mod_ty_   = 0.0f;
    float mod_tz_   = 0.0f;
    bool  mod_turbo_on_        = false;
    float mod_turbo_mult_      = 3.0f;
    int   mod_msg_ = 0; // 0=нет, 1=OK, 2=ошибка
    char  mod_msg_text_[160]   = {};

    InspectorMenuConfig                config_{};
    inspector::WorldDiagnosticSnapshot snapshot_{};
    bool                               use_external_snapshot_ = false;
    uint64_t                           last_snapshot_ms_      = 0;

    int                                active_section_        = 0; // 0..6
    int                                forced_tab_index_      = -1;
    bool                               pending_preset_apply_  = false;
    WindowPresetId                     pending_preset_        = WindowPresetId::StandardCenter;

    // Кольцевые буферы телеметрии для живых графиков (Sparklines)
    std::array<float, kHistorySamples> hist_fps_{};
    std::array<float, kHistorySamples> hist_frametime_ms_{};
    std::array<float, kHistorySamples> hist_ping_ms_{};
    std::array<float, kHistorySamples> hist_pkt_per_sec_{};
    std::array<float, kHistorySamples> hist_local_speed_kmh_{};
    size_t                             hist_cursor_           = 0;
    uint64_t                           last_hist_sample_ms_   = 0;

    // Вычисляемая кинематика локального игрока и трафика (Read-Only)
    bool                               has_prev_local_pos_    = false;
    float                              prev_local_pos_[3]     = {};
    uint64_t                           prev_local_pos_ms_     = 0;
    float                              computed_speed_ms_     = 0.0f;
    float                              computed_speed_kmh_    = 0.0f;
    double                             session_odometer_m_    = 0.0;
    uint32_t                           prev_sync_cache_33_[7] = {};
    uint64_t                           sync_cache_changed_ms_[7] = {};

    uint64_t                           prev_net_total_pkts_   = 0;
    uint64_t                           prev_net_total_rpcs_   = 0;
    uint64_t                           prev_net_rate_ms_      = 0;
    float                              rate_pkts_per_sec_     = 0.0f;
    float                              rate_rpcs_per_sec_     = 0.0f;

    // Кинетический скролл пальцем (Touch Drag-to-Scroll)
    float                              touch_scroll_vel_y_    = 0.0f;
    void*                              touch_scroll_win_      = nullptr;

    // Фильтры и избранное PlayerPool
    char                               player_search_buf_[64] = {};
    bool                               filter_players_ped_    = false;
    bool                               filter_players_veh_    = false;
    bool                               filter_players_pinned_ = false;
    float                              player_max_dist_m_     = 3000.0f;
    int                                selected_remote_slot_  = -1;
    std::array<bool, 1504>             pinned_players_{};

    // Фильтры VehiclePool
    char                               vehicle_search_buf_[32] = {};
    int                                vehicle_quick_preset_  = 0; // 0=Все, 1=<150м, 2=Движутся, 3=Подтип 11, 4=С CVehicle*
    int                                selected_vehicle_slot_ = -1;

    // Фильтры Сети и Byte-Diff
    int                                net_subtab_            = 0;
    bool                               rpc_only_strict_       = false;
    char                               rpc_search_buf_[32]    = {};
    int                                log_filter_kind_       = 0;
    int                                log_filter_id_         = -1;
    uint64_t                           selected_event_seq_    = 0;
    bool                               highlight_byte_diff_   = true;

    // Лаборатория памяти (Hex + Disasm + Watchlist + Read-Only Scanner + История)
    int                                mem_lab_mode_          = 0; // 0=Hex, 1=ARM64 Disasm, 2=Watchlist, 3=Сканер, 4=Карта VA
    char                               mem_addr_input_[32]    = {};
    uintptr_t                          mem_inspect_addr_      = 0;
    char                               mem_inspect_label_[96] = "Не выбран";
    size_t                             mem_inspect_bytes_     = 256;
    size_t                             mem_selected_offset_   = 0;
    std::vector<uintptr_t>             mem_nav_history_;

    std::vector<MemoryWatchItem>       watch_items_;
    char                               watch_new_addr_[32]    = {};
    char                               watch_new_label_[64]   = {};
    int                                watch_new_type_        = static_cast<int>(WatchValueType::Float32);

    int                                scan_mode_             = 0; // 0=Hex паттерн, 1=Int32, 2=Float, 3=ASCII строка
    char                               scan_query_buf_[96]    = {};
    char                               scan_start_buf_[32]    = {};
    int                                scan_range_kb_         = 64;
    std::vector<MemoryScanHit>         scan_results_;
    char                               scan_status_msg_[128]  = "Готов к Read-Only поиску в выбранном диапазоне";

    // Статус экспорта файлов
    char                               export_status_msg_[256] = "Файлы ещё не экспортировались в этой сессии.";
    bool                               export_last_ok_         = true;
};

} // namespace br::ui
