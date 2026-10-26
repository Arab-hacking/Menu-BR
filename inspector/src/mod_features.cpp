// ============================================================================
// mod_features.cpp — реализация функций мода для 16.111.19050.
// Записи — только в игровые структуры (CPed/CVehicle/CLocalPlayer) через
// process_vm_writev; чтения — br::mem::SafeMemory / br::inspector::GameReader.
// ============================================================================

#include "mod_features.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#define BR_MOD_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, kLogTag, fmt, ##__VA_ARGS__)
#else
#define BR_MOD_LOGI(fmt, ...) std::fprintf(stdout, "[BRMod][INFO] " fmt "\n", ##__VA_ARGS__)
#endif

#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "br_offsets_16_111_19050.hpp"
#include "internal_imgui_bridge.hpp"

namespace br::mem {

namespace {
ssize_t KernelProcessVmWritev(pid_t pid,
                              const struct iovec* local_iov,
                              unsigned long liovcnt,
                              const struct iovec* remote_iov,
                              unsigned long riovcnt,
                              unsigned long flags) noexcept {
#if defined(SYS_process_vm_writev)
    return ::syscall(SYS_process_vm_writev, pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
#elif defined(__NR_process_vm_writev)
    return ::syscall(__NR_process_vm_writev, pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
#else
    (void)pid; (void)local_iov; (void)liovcnt; (void)remote_iov; (void)riovcnt; (void)flags;
    return -1;
#endif
}
} // namespace

bool WriteBytes(uintptr_t addr, const void* in_buf, size_t size) noexcept {
    if (!in_buf || size == 0) {
        return false;
    }
    if (!SafeMemory::IsPlausibleAddress(addr)) {
        return false;
    }
    const uintptr_t clean_addr = SafeMemory::StripTag(addr);
    if (clean_addr + size < clean_addr) {
        return false; // переполнение адреса
    }

    static const pid_t self_pid = ::getpid();
    struct iovec local{};
    local.iov_base = const_cast<void*>(in_buf);
    local.iov_len  = size;

    struct iovec remote{};
    remote.iov_base = reinterpret_cast<void*>(clean_addr);
    remote.iov_len  = size;

    ssize_t n = KernelProcessVmWritev(self_pid, &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(size)) {
        return true;
    }
    return false;
}

} // namespace br::mem

namespace br::mod {

namespace {

constexpr const char* kLogTag = "BRMod";

uintptr_t CurrentModuleBase() {
    return br::ui::InternalImGuiBridge::GetRuntimeTable().module_base;
}

bool ReadPtr(uintptr_t addr, uintptr_t& out) {
    return br::mem::SafeMemory::Read(addr, out);
}

} // namespace

inspector::WorldDiagnosticSnapshot Snapshot(uintptr_t module_base) {
    inspector::WorldDiagnosticSnapshot snap{};
    if (module_base != 0) {
        inspector::GameReader::CaptureSnapshot(module_base, snap, /*allow_rakclient_vcall=*/true);
    }
    return snap;
}

ActionResult TeleportLocal(float x, float y, float z) {
    inspector::WorldDiagnosticSnapshot snap = Snapshot(CurrentModuleBase());
    const uintptr_t local = snap.local_player.local_player_ptr;
    const uintptr_t ped   = snap.local_player.ped_ptr;

    uintptr_t target = 0;
    const uintptr_t veh_ptr = snap.local_player.local_vehicle_ptr;
    if (veh_ptr != 0) {
        // Игрок за рулём — телепортим машину
        target = veh_ptr;
    } else if (ped != 0) {
        target = ped;
    }
    if (target == 0) {
        return {false, "Локальный игрок/машина не найдены"};
    }

    if (!br::mem::WriteBytes(target + br::offsets::player::kPedPosXOffset, &x, sizeof(x)) ||
        !br::mem::WriteBytes(target + br::offsets::player::kPedPosYOffset, &y, sizeof(y)) ||
        !br::mem::WriteBytes(target + br::offsets::player::kPedPosZOffset, &z, sizeof(z))) {
        return {false, "Запись позиции не удалась"};
    }
    BR_MOD_LOGI(
                        "Телепорт: %s -> (%.2f, %.2f, %.2f)",
                        veh_ptr != 0 ? "машина" : "пед", x, y, z);
    return {true, "Телепорт выполнен"};
}

ActionResult HealLocal() {
    inspector::WorldDiagnosticSnapshot snap = Snapshot(CurrentModuleBase());
    const uintptr_t ped = snap.local_player.ped_ptr;
    if (ped == 0) {
        return {false, "Локальный пед не найден"};
    }
    const float full = 100.0f;
    if (!br::mem::WriteBytes(ped + br::offsets::player::kPedFloat4B0Offset, &full, sizeof(full)) ||
        !br::mem::WriteBytes(ped + br::offsets::player::kPedFloat4B8Offset, &full, sizeof(full))) {
        return {false, "Запись HP/Armor не удалась"};
    }
    BR_MOD_LOGI( "Лечилка: HP=100 Armor=100");
    return {true, "HP/Armor = 100"};
}

ActionResult EnterVehicleById(uint16_t vehicle_id) {
    inspector::WorldDiagnosticSnapshot snap = Snapshot(CurrentModuleBase());
    const uintptr_t ped   = snap.local_player.ped_ptr;
    const uintptr_t local = snap.local_player.local_player_ptr;
    if (ped == 0 || local == 0) {
        return {false, "Локальный игрок не найден"};
    }

    const inspector::VehicleEntry* found = nullptr;
    for (const auto& v : snap.vehicles) {
        if (v.game_vehicle_valid && v.vehicle_id == vehicle_id) {
            found = &v;
            break;
        }
    }
    if (!found || found->game_vehicle_ptr == 0) {
        return {false, "Машина не найдена в VehiclePool"};
    }

    const uintptr_t game_veh = found->game_vehicle_ptr;

    if (!br::mem::WriteBytes(ped + br::offsets::player::kPedVehiclePtrOffset, &game_veh, sizeof(game_veh)) ||
        !br::mem::WriteBytes(local + br::offsets::player::kLocalVehiclePtrOffset, &game_veh, sizeof(game_veh))) {
        return {false, "Запись указателей машины не удалась"};
    }

    const uint8_t in_veh = 1;
    if (!br::mem::WriteBytes(ped + br::offsets::player::kPedInVehicleFlagOffset, &in_veh, sizeof(in_veh))) {
        return {false, "Запись флага посадки не удалась"};
    }

    // Backref машины на пед (связь apply path)
    if (!br::mem::WriteBytes(game_veh + br::offsets::vehicle::kVehBackRefB8Offset, &ped, sizeof(ped))) {
        return {false, "Запись backref не удалась"};
    }

    // Подставляем пед рядом с машиной (посадочная точка = позиция машины)
    float px = found->pos[0];
    float py = found->pos[1];
    float pz = found->pos[2];
    br::mem::WriteBytes(ped + br::offsets::player::kPedPosXOffset, &px, sizeof(px));
    br::mem::WriteBytes(ped + br::offsets::player::kPedPosYOffset, &py, sizeof(py));
    br::mem::WriteBytes(ped + br::offsets::player::kPedPosZOffset, &pz, sizeof(pz));

    BR_MOD_LOGI( "Посадка: veh id=%u @ 0x%lx",
                        vehicle_id, static_cast<unsigned long>(game_veh));
    return {true, "Посадка выполнена"};
}

// --- Турбо ------------------------------------------------------------------

namespace {
bool  g_turbo_on     = false;
float g_turbo_mult   = 3.0f;
float g_turbo_base   = 10.0f; // базовая скорость, зафиксированная при включении
} // namespace

void SetTurbo(bool on, float mult) {
    mult = std::clamp(mult, 1.5f, 8.0f);
    if (on && !g_turbo_on) {
        // Зафиксируем текущую скорость машины как базу
        const uintptr_t base = CurrentModuleBase();
        uintptr_t pool = 0, local = 0, veh = 0;
        if (base != 0 &&
            ReadPtr(base + br::offsets::globals::kPlayerPoolPtr, pool) && pool != 0 &&
            ReadPtr(pool + br::offsets::player::kPoolLocalPlayerPtrOffset, local) && local != 0 &&
            ReadPtr(local + br::offsets::player::kLocalVehiclePtrOffset, veh) && veh != 0) {
            float vel[3] = {0.0f, 0.0f, 0.0f};
            if (br::mem::SafeMemory::Read(veh + br::offsets::vehicle::kVehMoveSpeedOffset, vel)) {
                const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
                g_turbo_base = std::clamp(speed, 5.0f, 90.0f);
            }
        }
        if (g_turbo_base < 1.0f) {
            g_turbo_base = 10.0f;
        }
        BR_MOD_LOGI(
                            "Турбо ВКЛ, база=%.1f mult=%.1f -> цель %.1f",
                            g_turbo_base, mult, g_turbo_base * mult);
    }
    g_turbo_on   = on;
    g_turbo_mult = mult;
}

bool TurboEnabled() noexcept {
    return g_turbo_on;
}

void Tick() {
    if (!g_turbo_on) {
        return;
    }

    const uintptr_t base = CurrentModuleBase();
    if (base == 0) {
        return;
    }

    uintptr_t pool = 0, local = 0, veh = 0;
    if (!ReadPtr(base + br::offsets::globals::kPlayerPoolPtr, pool) || pool == 0 ||
        !ReadPtr(pool + br::offsets::player::kPoolLocalPlayerPtrOffset, local) || local == 0 ||
        !ReadPtr(local + br::offsets::player::kLocalVehiclePtrOffset, veh) || veh == 0) {
        return; // игрока или машины нет
    }

    float vel[3] = {0.0f, 0.0f, 0.0f};
    if (!br::mem::SafeMemory::Read(veh + br::offsets::vehicle::kVehMoveSpeedOffset, vel)) {
        return;
    }

    const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    const float target = g_turbo_base * g_turbo_mult;
    if (speed <= 0.5f || speed >= target) {
        return; // стоим или уже достигли цели
    }

    // Ускоряем в текущем направлении
    const float scale = target / speed;
    vel[0] *= scale;
    vel[1] *= scale;
    vel[2] *= scale;
    br::mem::WriteBytes(veh + br::offsets::vehicle::kVehMoveSpeedOffset, vel, sizeof(vel));
}

} // namespace br::mod
