#pragma once

// ============================================================================
// mod_features — функции мода (телепорт, лечилка, посадка, турбо) поверх
// референсного инспектора br_imgui_inspector_16.111.19050.
//
// Чтение — через br::inspector::GameReader (read-only, process_vm_readv).
// Запись — br::mem::SafeWrite (зеркало SafeMemory, process_vm_writev:
// ядро возвращает EFAULT вместо SIGSEGV при невалидной странице).
// Все действия выполняются на GL-потоке из UI-кнопок (см. inspector_menu.cpp).
// ============================================================================

#include <cstdint>

#include "game_reader.hpp"
#include "safe_memory.hpp"

namespace br::mem {

// Запись памяти процесса (зеркало SafeMemory::ReadBytes на process_vm_writev)
bool WriteBytes(uintptr_t addr, const void* in_buf, size_t size) noexcept;

} // namespace br::mem

namespace br::mod {

struct ActionResult {
    bool        ok    = false;
    const char* msg   = "OK";
};

// Живой снимок мира (read-only) — то же, что использует инспектор
inspector::WorldDiagnosticSnapshot Snapshot(uintptr_t module_base);

// --- Действия (пишут только в игровые структуры через SafeWrite) -----------

// Телепорт локального игрока (или его машины, если он в машине)
ActionResult TeleportLocal(float x, float y, float z);

// Лечилка: HP + Armor локального педa = 100
ActionResult HealLocal();

// Посадка в машину по её CVehicle id (скан VehiclePool):
//   ped+0x540 = gameVeh, ped+0x548 = 1, gameVeh+0xB8 = ped (backref),
//   LocalPlayer+0x40 = gameVeh, телепорт педa к машине
ActionResult EnterVehicleById(uint16_t vehicle_id);

// --- Турбо (per-frame) ------------------------------------------------------

// on/mult: включать и задавать множитель. Базовая скорость фиксируется
// в момент включения (текущая скорость машины, clamp [1, 90] * mult).
void SetTurbo(bool on, float mult);
bool TurboEnabled() noexcept;

// Вызывается каждый кадр из рендер-колбэка инспектора (GL-поток):
// подталкивает локальную машину к базовой скорости * mult.
void Tick();

} // namespace br::mod
