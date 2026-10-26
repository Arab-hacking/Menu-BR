#pragma once

// ============================================================================
// got_hook — перехват вызовов через PLT/GOT целевого модуля БЕЗ RWX-памяти.
//
// Механизм: в ELF модуля ищем .rela.plt (R_AARCH64_JUMP_SLOT) для нужного
// символа (например eglSwapBuffers) и записываем наш detour прямо в слот
// GOT. Страница .got уже отображена лоадером как RW — RWX/mprotect/execmem
// не нужны вообще. Оригинальная функция = прежнее значение слота.
//
// Ограничение: перехватываются только вызовы ИЗ ЭТОГО модуля (через его
// PLT) — для eglSwapBuffers это ровно GL-поток игры (то, что и нужно).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace br::hook {

struct GotHookRecord {
    uintptr_t got_slot_va = 0;
    uintptr_t saved_value = 0; // оригинальный адрес (резолв. функция)
    bool      active      = false;
    std::string symbol;
};

// Найти и перехватить symbol в модуле на module_base.
// out_original: предыдущее значение слота GOT (адрес оригинальной функции).
bool InstallGotHook(uintptr_t module_base,
                    const char* symbol,
                    uintptr_t detour,
                    uintptr_t* out_original);

// Вернуть все установленные GOT-хуки (вызывать при Detach)
bool RestoreGotHooks();

const std::vector<GotHookRecord>& GetGotHooks();

// Файловый офсет первого исполняемого PT_LOAD-сегмента в ELF-файле path
// (для file-backed RWX-трамплина). 0 = не найден/ошибка.
uintptr_t FindFirstExecSegmentFileOffset(const char* path, size_t* out_size);

} // namespace br::hook
