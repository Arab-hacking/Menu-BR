#include "game_reader.hpp"

#include <cmath>
#include "safe_memory.hpp"

namespace br::inspector {

namespace {

float Distance3D(const float a[3], const float b[3]) noexcept {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void ReadLocalPlayer(uintptr_t player_pool_ptr, LocalPlayerSnapshot& out) {
    out = LocalPlayerSnapshot{};
    if (!mem::SafeMemory::IsPlausibleAddress(player_pool_ptr)) {
        return;
    }

    using namespace offsets::player;
    uintptr_t local_ptr = 0;
    if (!mem::SafeMemory::Read(player_pool_ptr + kPoolLocalPlayerPtrOffset, local_ptr) ||
        !mem::SafeMemory::IsPlausibleAddress(local_ptr)) {
        return;
    }

    out.local_player_ptr = local_ptr;
    out.valid            = true;
    mem::SafeMemory::Read(local_ptr + kLocalIdOffset, out.player_id);
    out.name = mem::SafeMemory::ReadCString(local_ptr + kLocalNameOffset, kLocalNameMaxRead);

    mem::SafeMemory::ReadBytes(local_ptr + kLocalCacheBlockOffset,
                               out.cache_block_f0.data(),
                               out.cache_block_f0.size());
    mem::SafeMemory::Read(local_ptr + kLocalSyncField1F4Offset, out.sync_field_1f4);
    mem::SafeMemory::Read(local_ptr + kLocalSyncField1F8Offset, out.sync_field_1f8);
    mem::SafeMemory::Read(local_ptr + kLocalSyncField200Offset, out.sync_field_200);

    if (mem::SafeMemory::Read(local_ptr + kLocalPedPtrOffset, out.ped_ptr) &&
        mem::SafeMemory::IsPlausibleAddress(out.ped_ptr)) {
        out.ped_valid = mem::SafeMemory::ReadBytes(out.ped_ptr + kPedPosXOffset, out.pos, sizeof(out.pos));
        mem::SafeMemory::Read(out.ped_ptr + kPedMatrixPtrOffset, out.matrix_ptr);
        mem::SafeMemory::Read(out.ped_ptr + kPedFloat4B0Offset, out.ped_float_4b0);
        mem::SafeMemory::Read(out.ped_ptr + kPedFloat4B8Offset, out.ped_float_4b8);
        mem::SafeMemory::Read(out.ped_ptr + kPedVehiclePtrOffset, out.ped_vehicle_ptr);
        mem::SafeMemory::Read(out.ped_ptr + kPedInVehicleFlagOffset, out.ped_in_vehicle_flag);
    }

    if (mem::SafeMemory::Read(local_ptr + kLocalVehiclePtrOffset, out.local_vehicle_ptr) &&
        mem::SafeMemory::IsPlausibleAddress(out.local_vehicle_ptr)) {
        mem::SafeMemory::Read(out.local_vehicle_ptr + offsets::vehicle::kVehIdOffset,
                              out.local_vehicle_id);
        mem::SafeMemory::Read(out.local_vehicle_ptr + offsets::vehicle::kVehClassSubTypeOffset,
                              out.local_vehicle_subtype);
    }
}

void ReadRemotePlayers(uintptr_t player_pool_ptr,
                       const LocalPlayerSnapshot& local,
                       std::vector<RemotePlayerEntry>& out_list) {
    out_list.clear();
    if (!mem::SafeMemory::IsPlausibleAddress(player_pool_ptr)) {
        return;
    }

    using namespace offsets::player;
    // Читаем всю таблицу слотов (1504 * 8 = 12 032 Б) одним системным вызовом для скорости
    std::array<uintptr_t, kPlayerPoolSlotCount> slot_ptrs{};
    if (!mem::SafeMemory::ReadBytes(player_pool_ptr + kPoolRemoteSlotsBaseOffset,
                                    slot_ptrs.data(),
                                    sizeof(slot_ptrs))) {
        return;
    }

    std::array<int32_t, kPlayerPoolSlotCount> aux_a{};
    std::array<int32_t, kPlayerPoolSlotCount> aux_b{};
    mem::SafeMemory::ReadBytes(player_pool_ptr + kPoolAuxInt32ArrayAOffset, aux_a.data(), sizeof(aux_a));
    mem::SafeMemory::ReadBytes(player_pool_ptr + kPoolAuxInt32ArrayBOffset, aux_b.data(), sizeof(aux_b));

    out_list.reserve(128);
    for (size_t id = 0; id < kPlayerPoolSlotCount; ++id) {
        const uintptr_t rptr = slot_ptrs[id];
        if (!mem::SafeMemory::IsPlausibleAddress(rptr)) {
            continue;
        }

        RemotePlayerEntry entry{};
        entry.slot_id    = static_cast<uint16_t>(id);
        entry.remote_ptr = rptr;
        entry.pool_aux_a = aux_a[id];
        entry.pool_aux_b = aux_b[id];

        if (!mem::SafeMemory::Read(rptr + kRemoteIdOffset, entry.reported_id)) {
            continue;
        }
        entry.name = mem::SafeMemory::ReadCString(rptr + kRemoteNameOffset, kRemoteNameMaxRead);
        mem::SafeMemory::Read(rptr + kRemoteState120Offset, entry.state_120);
        mem::SafeMemory::Read(rptr + kRemoteField150Offset, entry.field_150);
        mem::SafeMemory::Read(rptr + kRemoteByte15BOffset, entry.byte_15b);
        mem::SafeMemory::Read(rptr + kRemoteU16_15COffset, entry.u16_15c);

        if (mem::SafeMemory::Read(rptr + kRemotePedPtrOffset, entry.ped_ptr) &&
            mem::SafeMemory::IsPlausibleAddress(entry.ped_ptr)) {
            entry.ped_valid =
                mem::SafeMemory::ReadBytes(entry.ped_ptr + kPedPosXOffset, entry.pos, sizeof(entry.pos));
            if (entry.ped_valid && local.ped_valid) {
                entry.distance_to_local = Distance3D(entry.pos, local.pos);
            }
            mem::SafeMemory::Read(entry.ped_ptr + kPedFloat4B0Offset, entry.ped_float_4b0);
            mem::SafeMemory::Read(entry.ped_ptr + kPedFloat4B8Offset, entry.ped_float_4b8);
            mem::SafeMemory::Read(entry.ped_ptr + kPedVehiclePtrOffset, entry.ped_vehicle_ptr);
            mem::SafeMemory::Read(entry.ped_ptr + kPedInVehicleFlagOffset, entry.ped_in_vehicle);
        }

        out_list.push_back(std::move(entry));
    }
}

void ReadVehicles(uintptr_t vehicle_pool_ptr,
                  const LocalPlayerSnapshot& local,
                  std::vector<VehicleEntry>& out_list) {
    out_list.clear();
    if (!mem::SafeMemory::IsPlausibleAddress(vehicle_pool_ptr)) {
        return;
    }

    using namespace offsets::vehicle;
    // Читаем 2000 слотов (16 000 Б) одним вызовом
    std::array<uintptr_t, kVehiclePoolSlotCount> net_veh_ptrs{};
    if (!mem::SafeMemory::ReadBytes(vehicle_pool_ptr, net_veh_ptrs.data(), sizeof(net_veh_ptrs))) {
        return;
    }

    out_list.reserve(128);
    for (size_t idx = 0; idx < kVehiclePoolSlotCount; ++idx) {
        const uintptr_t nv_ptr = net_veh_ptrs[idx];
        if (!mem::SafeMemory::IsPlausibleAddress(nv_ptr)) {
            continue;
        }

        VehicleEntry v{};
        v.slot_index      = static_cast<uint16_t>(idx);
        v.net_vehicle_ptr = nv_ptr;

        mem::SafeMemory::Read(nv_ptr + kNetVehGameVehPtrOffset, v.game_vehicle_ptr);
        mem::SafeMemory::Read(nv_ptr + kNetVehSubStatePtrOffset, v.sub_state_ptr);

        if (mem::SafeMemory::IsPlausibleAddress(v.game_vehicle_ptr)) {
            v.game_vehicle_valid =
                mem::SafeMemory::ReadBytes(v.game_vehicle_ptr + kVehPosXOffset, v.pos, sizeof(v.pos));
            mem::SafeMemory::ReadBytes(v.game_vehicle_ptr + kVehMoveSpeedOffset,
                                       v.move_speed,
                                       sizeof(v.move_speed));
            mem::SafeMemory::ReadBytes(v.game_vehicle_ptr + kVehTurnSpeedOffset,
                                       v.turn_speed,
                                       sizeof(v.turn_speed));
            v.speed_magnitude = std::sqrt(v.move_speed[0] * v.move_speed[0] +
                                          v.move_speed[1] * v.move_speed[1] +
                                          v.move_speed[2] * v.move_speed[2]);
            if (v.game_vehicle_valid && local.ped_valid) {
                v.distance_to_local = Distance3D(v.pos, local.pos);
            }
            mem::SafeMemory::Read(v.game_vehicle_ptr + kVehIdOffset, v.vehicle_id);
            mem::SafeMemory::Read(v.game_vehicle_ptr + kVehClassSubTypeOffset, v.class_subtype);
            mem::SafeMemory::Read(v.game_vehicle_ptr + kVehBackRefB8Offset, v.back_ref_b8);
            mem::SafeMemory::Read(v.game_vehicle_ptr + kVehSpecialFloat790Offset, v.special_float_790);

            v.is_local_vehicle =
                (local.local_vehicle_ptr != 0 && v.game_vehicle_ptr == local.local_vehicle_ptr) ||
                (local.ped_vehicle_ptr != 0 && v.game_vehicle_ptr == local.ped_vehicle_ptr);
        }

        out_list.push_back(v);
    }
}

size_t CountActiveObjectSlots(uintptr_t object_pool_ptr) {
    if (!mem::SafeMemory::IsPlausibleAddress(object_pool_ptr)) {
        return 0;
    }
    // В 0x17DC978 цикл проходит 0..0x8000 с шагом 8 -> 4096 указателей
    std::array<uintptr_t, 4096> slots{};
    if (!mem::SafeMemory::ReadBytes(object_pool_ptr, slots.data(), sizeof(slots))) {
        return 0;
    }
    size_t count = 0;
    for (uintptr_t p : slots) {
        if (mem::SafeMemory::IsPlausibleAddress(p)) {
            ++count;
        }
    }
    return count;
}

void ReadRakClientStatus(uintptr_t module_base,
                         RakClientStatusSnapshot& out,
                         bool allow_vcall) {
    out = RakClientStatusSnapshot{};
    if (module_base == 0) {
        return;
    }

    using namespace offsets::raknet;
    if (!mem::SafeMemory::Read(module_base + offsets::globals::kRakClientInterfacePtr,
                               out.rakclient_ptr) ||
        !mem::SafeMemory::IsPlausibleAddress(out.rakclient_ptr)) {
        return;
    }

    if (!mem::SafeMemory::Read(out.rakclient_ptr, out.vtable_ptr)) {
        return;
    }

    const uintptr_t expected_vt = module_base + kRakClientInterfaceVtableVA;
    out.vtable_verified =
        (mem::SafeMemory::StripTag(out.vtable_ptr) == mem::SafeMemory::StripTag(expected_vt));

#if defined(__aarch64__)
    if (allow_vcall && out.vtable_verified) {
        using FnIsConnected = bool (*)(const void*);
        using FnGetPing     = int (*)(const void*);
        using FnGetAvgPing  = int (*)(void*);
        using FnGetMtu      = int (*)(const void*);

        uintptr_t fn_conn = 0, fn_last = 0, fn_avg = 0, fn_low = 0, fn_mtu = 0;
        if (mem::SafeMemory::Read(out.vtable_ptr + kVtableSlotIsConnected, fn_conn) &&
            mem::SafeMemory::Read(out.vtable_ptr + kVtableSlotGetLastPing, fn_last) &&
            mem::SafeMemory::Read(out.vtable_ptr + kVtableSlotGetAveragePing, fn_avg) &&
            mem::SafeMemory::Read(out.vtable_ptr + kVtableSlotGetLowestPing, fn_low) &&
            mem::SafeMemory::Read(out.vtable_ptr + kVtableSlotGetMtuSize, fn_mtu)) {
            const void* self_c = reinterpret_cast<const void*>(out.rakclient_ptr);
            void*       self_m = reinterpret_cast<void*>(out.rakclient_ptr);

            out.queried_live_methods = true;
            out.is_connected = reinterpret_cast<FnIsConnected>(fn_conn)(self_c);
            if (out.is_connected) {
                out.last_ping    = reinterpret_cast<FnGetPing>(fn_last)(self_c);
                out.average_ping = reinterpret_cast<FnGetAvgPing>(fn_avg)(self_m);
                out.lowest_ping  = reinterpret_cast<FnGetPing>(fn_low)(self_c);
            }
            out.mtu_size = reinterpret_cast<FnGetMtu>(fn_mtu)(self_c);
        }
    }
#else
    (void)allow_vcall;
#endif
}

} // namespace

void GameReader::CaptureSnapshot(uintptr_t module_base,
                                 WorldDiagnosticSnapshot& out_snap,
                                 bool allow_rakclient_vcall) {
    out_snap.module_base = module_base;
    if (module_base == 0) {
        return;
    }

    using namespace offsets::globals;
    mem::SafeMemory::Read(module_base + kPlayerPoolPtr, out_snap.player_pool_ptr);
    mem::SafeMemory::Read(module_base + kVehiclePoolPtr, out_snap.vehicle_pool_ptr);
    mem::SafeMemory::Read(module_base + kObjectPoolPtr, out_snap.object_pool_ptr);
    mem::SafeMemory::Read(module_base + kPickupOrAuxPoolPtr, out_snap.pickup_aux_pool_ptr);
    mem::SafeMemory::Read(module_base + kTextLabelOrAuxPoolPtr, out_snap.textlabel_aux_pool_ptr);
    mem::SafeMemory::Read(module_base + kGangZoneOrAuxPoolPtr, out_snap.gangzone_aux_pool_ptr);

    ReadRakClientStatus(module_base, out_snap.rakclient, allow_rakclient_vcall);
    ReadLocalPlayer(out_snap.player_pool_ptr, out_snap.local_player);
    ReadRemotePlayers(out_snap.player_pool_ptr, out_snap.local_player, out_snap.remote_players);
    ReadVehicles(out_snap.vehicle_pool_ptr, out_snap.local_player, out_snap.vehicles);
    out_snap.active_object_slots = CountActiveObjectSlots(out_snap.object_pool_ptr);
}

} // namespace br::inspector
