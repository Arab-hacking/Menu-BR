#include "safe_memory.hpp"

#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace br::mem {

namespace {
CustomReadFn g_custom_reader = nullptr;
void*        g_custom_user   = nullptr;

ssize_t KernelProcessVmReadv(pid_t pid,
                             const struct iovec* local_iov,
                             unsigned long liovcnt,
                             const struct iovec* remote_iov,
                             unsigned long riovcnt,
                             unsigned long flags) noexcept {
#if defined(SYS_process_vm_readv)
    return ::syscall(SYS_process_vm_readv, pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
#elif defined(__NR_process_vm_readv)
    return ::syscall(__NR_process_vm_readv, pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
#else
    (void)pid; (void)local_iov; (void)liovcnt; (void)remote_iov; (void)riovcnt; (void)flags;
    return -1;
#endif
}
} // namespace

void SafeMemory::SetCustomReader(CustomReadFn fn, void* user_data) noexcept {
    g_custom_reader = fn;
    g_custom_user   = user_data;
}

bool SafeMemory::ReadBytes(uintptr_t addr, void* out_buf, size_t size) noexcept {
    if (!out_buf || size == 0) {
        return false;
    }

    if (g_custom_reader) {
        return g_custom_reader(addr, out_buf, size, g_custom_user);
    }

    if (!IsPlausibleAddress(addr)) {
        return false;
    }

    const uintptr_t clean_addr = StripTag(addr);
    if (clean_addr + size < clean_addr) {
        return false; // переполнение адреса
    }

    static const pid_t self_pid = ::getpid();
    struct iovec local{};
    local.iov_base = out_buf;
    local.iov_len  = size;

    // Передаём исходный addr (на Android с Tagged Pointers ядро понимает untagged/tagged)
    struct iovec remote{};
    remote.iov_base = reinterpret_cast<void*>(clean_addr);
    remote.iov_len  = size;

    ssize_t n = KernelProcessVmReadv(self_pid, &local, 1, &remote, 1, 0);
    if (n == static_cast<ssize_t>(size)) {
        return true;
    }

    // Если на конкретном ядре tagged pointer нужен без очистки
    if (clean_addr != addr) {
        remote.iov_base = reinterpret_cast<void*>(addr);
        n = KernelProcessVmReadv(self_pid, &local, 1, &remote, 1, 0);
        if (n == static_cast<ssize_t>(size)) {
            return true;
        }
    }

    return false;
}

std::string SafeMemory::ReadCString(uintptr_t addr, size_t max_len) {
    if (max_len == 0 || max_len > 512) {
        max_len = 64;
    }
    char buf[513]{};
    // Читаем блоками или целиком; если буфер на границе страницы, пробуем побайтно fallback
    if (!ReadBytes(addr, buf, max_len)) {
        size_t ok = 0;
        for (; ok < max_len; ++ok) {
            if (!ReadBytes(addr + ok, &buf[ok], 1)) {
                break;
            }
            if (buf[ok] == '\0') {
                break;
            }
        }
        buf[ok] = '\0';
    } else {
        buf[max_len] = '\0';
    }

    std::string result;
    result.reserve(max_len);
    for (size_t i = 0; i < max_len && buf[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(buf[i]);
        // Оставляем печатный ASCII и валидные байты UTF-8 кириллицы
        if (c >= 0x20 && c != 0x7F) {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

} // namespace br::mem
