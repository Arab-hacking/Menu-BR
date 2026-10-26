#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace br::hook {

// Внешний бэкенд хуков (например DobbyHook / ShadowHook), если пользователь хочет подключить свой
using ExternalHookBackendFn = bool (*)(void* target, void* detour, void** out_original);

struct HookRecord {
    uintptr_t target_addr    = 0;
    uintptr_t detour_addr    = 0;
    uintptr_t trampoline_va  = 0;
    size_t    trampoline_len = 0;
    uint32_t  saved_insns[4] = {};
    bool      active         = false;
    bool      used_external  = false;
};

class Arm64Hook {
public:
    // Опциональная привязка к Dobby / ShadowHook / And64InlineHook
    static void SetExternalBackend(ExternalHookBackendFn backend_fn) noexcept;

    // Подсказка для file-backed RWX-трамплина (fallback, если анонимный
    // RWX-mmap запрещён SELinux): путь к читаемому исполняемому ELF (напр.,
    // сам .so игры). Трамплин создаётся как RW-маппинг его exec-сегмента.
    static void SetTrampolineFileHint(const char* path) noexcept;

    // Генератор релоцированного трамплина AArch64 (чистая функция, пригодна для офлайн-верификации):
    // Корректно релоцирует PC-зависимые инструкции ADRP, ADR, B, BL в 64-битные переходы.
    static bool BuildRelocatedTrampoline(const uint32_t orig_insns[4],
                                         uintptr_t orig_pc,
                                         std::vector<uint32_t>& out_trampoline_words);

    // Кодирование 16-байтового абсолютного перехода AArch64 через IP1 (X17):
    //   ldr x17, #8       (0x58000051)
    //   br  x17           (0xD61F0220)
    //   .quad dest_addr
    static void EncodeAbsoluteJumpX17(uintptr_t dest_addr, uint32_t out_words[4]) noexcept;

    // Установка inline-перехвата на функцию AArch64 (на Android arm64-v8a)
    static bool Install(void* target, void* detour, void** out_original);

    // Снятие всех установленных перехватов при выгрузке модуля
    static void RemoveAll() noexcept;

    static const std::vector<HookRecord>& GetInstalledHooks() noexcept;
};

} // namespace br::hook
