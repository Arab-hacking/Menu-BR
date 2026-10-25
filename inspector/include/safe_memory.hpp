#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace br::mem {

using CustomReadFn = bool (*)(uintptr_t address, void* out_buf, size_t size, void* user_data);

// Безопасное чтение памяти процесса только для чтения (Read-Only).
// Использует системный вызов process_vm_readv (ядро Linux/Android возвращает -EFAULT
// вместо генерации SIGSEGV при обращении к размапленной/невалидной странице).
class SafeMemory {
public:
    static constexpr uintptr_t kMinValidUserAddr = 0x0000000000010000ULL;
    static constexpr uintptr_t kMaxValidUserAddr = 0x0000FFFFFFFFFFFFULL;

    static bool IsPlausibleAddress(uintptr_t addr) noexcept {
        // Учёт ARM64 Top-Byte-Ignore (TBI / MTE теги в старшем байте на Android 11+)
        const uintptr_t untagged = StripTag(addr);
        return untagged >= kMinValidUserAddr && untagged <= kMaxValidUserAddr;
    }

    static uintptr_t StripTag(uintptr_t addr) noexcept {
#if defined(__aarch64__)
        return addr & 0x00FFFFFFFFFFFFFFULL;
#else
        return addr;
#endif
    }

    static bool ReadBytes(uintptr_t addr, void* out_buf, size_t size) noexcept;

    template <typename T>
    static bool Read(uintptr_t addr, T& out_val) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "SafeMemory::Read requires trivially copyable T");
        return ReadBytes(addr, &out_val, sizeof(T));
    }

    template <typename T>
    static T ReadOr(uintptr_t addr, T fallback = T{}) noexcept {
        T val{};
        return Read<T>(addr, val) ? val : fallback;
    }

    static std::string ReadCString(uintptr_t addr, size_t max_len = 64);

    // Хук чтения для модульных тестов / офлайн-дампов памяти
    static void SetCustomReader(CustomReadFn fn, void* user_data = nullptr) noexcept;
};

} // namespace br::mem
