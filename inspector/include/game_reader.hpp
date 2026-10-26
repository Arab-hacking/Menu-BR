#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "br_offsets_16_111_19050.hpp"

namespace br::inspector {

struct LocalPlayerSnapshot {
    bool      valid                 = false;
    uintptr_t local_player_ptr      = 0;
    uint16_t  player_id             = 0xFFFF;
    std::string name;

    uintptr_t ped_ptr               = 0;
    bool      ped_valid             = false;
    float     pos[3]                = {0.0f, 0.0f, 0.0f};
    uintptr_t matrix_ptr            = 0;
    float     ped_float_4b0         = 0.0f; // конвертируется в u16 в пакетах 30/33
    float     ped_float_4b8         = 0.0f; // конвертируется в u16 в пакетах 30/33
    uintptr_t ped_vehicle_ptr       = 0;    // CPed+0x540
    uint8_t   ped_in_vehicle_flag   = 0;    // CPed+0x548

    uintptr_t local_vehicle_ptr     = 0;    // LocalPlayer+0x40
    uint16_t  local_vehicle_id      = 0xFFFF;
    uint32_t  local_vehicle_subtype = 0;

    // Кэш состояния для пакета 33 (+0xF0..+0x10B) и поля синхронизации пакета 30 (+0x1F4..+0x200)
    std::array<uint8_t, offsets::player::kLocalCacheBlockSize> cache_block_f0{};
    uint32_t  sync_field_1f4        = 0;
    uint32_t  sync_field_1f8        = 0;
    uint32_t  sync_field_200        = 0;
};

struct RemotePlayerEntry {
    uint16_t    slot_id           = 0;
    uintptr_t   remote_ptr        = 0;
    uint16_t    reported_id       = 0;    // +0x4A
    std::string name;                     // +0x18 (до 49 байт)

    uintptr_t   ped_ptr           = 0;    // +0x10
    bool        ped_valid         = false;
    float       pos[3]            = {0.0f, 0.0f, 0.0f};
    float       distance_to_local = 0.0f;
    float       ped_float_4b0     = 0.0f;
    float       ped_float_4b8     = 0.0f;
    uintptr_t   ped_vehicle_ptr   = 0;
    uint8_t     ped_in_vehicle    = 0;

    uint32_t    state_120         = 0;    // +0x120
    uint64_t    field_150         = 0;    // +0x150
    uint8_t     byte_15b          = 0;    // +0x15B
    uint16_t    u16_15c           = 0;    // +0x15C

    int32_t     pool_aux_a        = 0;    // PlayerPool + 0x2F30 + id*4
    int32_t     pool_aux_b        = 0;    // PlayerPool + 0x46B0 + id*4
};

struct VehicleEntry {
    uint16_t  slot_index          = 0;
    uintptr_t net_vehicle_ptr     = 0;    // *(VehiclePool + slot*8)
    uintptr_t game_vehicle_ptr    = 0;    // *(CNetVehicle + 0x00)
    uintptr_t sub_state_ptr       = 0;    // *(CNetVehicle + 0x08)

    bool      game_vehicle_valid  = false;
    uint16_t  vehicle_id          = 0xFFFF; // CVehicle + 0x434
    uint32_t  class_subtype       = 0;      // CVehicle + 0x430
    float     pos[3]              = {0.0f, 0.0f, 0.0f};
    float     move_speed[3]       = {0.0f, 0.0f, 0.0f};
    float     turn_speed[3]       = {0.0f, 0.0f, 0.0f};
    float     speed_magnitude     = 0.0f;
    float     distance_to_local   = 0.0f;
    uintptr_t back_ref_b8         = 0;      // CVehicle + 0xB8
    float     special_float_790   = 0.0f;   // CVehicle + 0x790
    bool      is_local_vehicle    = false;
};

struct RakClientStatusSnapshot {
    uintptr_t rakclient_ptr           = 0;
    uintptr_t vtable_ptr              = 0;
    bool      vtable_verified         = false;
    bool      queried_live_methods    = false;
    bool      is_connected            = false;
    int       last_ping               = -1;
    int       average_ping            = -1;
    int       lowest_ping             = -1;
    int       mtu_size                = 0;
};

struct WorldDiagnosticSnapshot {
    uintptr_t module_base             = 0;
    uintptr_t player_pool_ptr         = 0;
    uintptr_t vehicle_pool_ptr        = 0;
    uintptr_t object_pool_ptr         = 0;
    uintptr_t pickup_aux_pool_ptr     = 0;
    uintptr_t textlabel_aux_pool_ptr  = 0;
    uintptr_t gangzone_aux_pool_ptr   = 0;

    size_t    active_object_slots     = 0; // из 4096 слотов ObjectPool

    RakClientStatusSnapshot        rakclient;
    LocalPlayerSnapshot            local_player;
    std::vector<RemotePlayerEntry> remote_players;
    std::vector<VehicleEntry>      vehicles;
};

class GameReader {
public:
    // Полностью безопасный Read-Only снимок состояния пулов и структур.
    // Если allow_rakclient_vcall == true и vtable точно совпадает с 0x02140BF0,
    // вызывает константные геттеры пинга/MTU у RakClientInterface.
    static void CaptureSnapshot(uintptr_t module_base,
                                WorldDiagnosticSnapshot& out_snap,
                                bool allow_rakclient_vcall = true);
};

} // namespace br::inspector
