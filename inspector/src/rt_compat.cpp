// ============================================================================
// rt_compat.cpp — дополнительные runtime-символы для zig cross-сборки.
//
// Zig не бандлит статическую libstdc++/libc++: out-of-line методы
// (std::thread, std::mutex, std::string::reserve/push_back, chrono)
// и __cxa_guard_* остаются UND. Здесь они реализованы поверх pthread
// и стандартных syscalls — без зависимостей от libstdc++.
//
// Лейауты (zig libc++, aarch64, little-endian, C++17):
//   std::thread = { pthread_t __t_; }            // joinable = id != 0
//   std::mutex  = { pthread_mutex_t __m_; }      // PTHREAD_MUTEX_INITIALIZER == 0
//   std::string = { __rep 24 B }                 // SSO 23 + флаг-байт, см. ниже
//   __thread_struct = { void* __p_; }            // imp не используется в нашем
//                                                // пути (thread без аргументов)
// ============================================================================

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <ctime>
#include <pthread.h>
#include <unistd.h>

namespace {

// ---- __cxa_guard (function-local static'и) --------------------------------
int br_cxa_guard_acquire_impl(void* guard) {
    // Конвенция libgcc: struct __guard { int _2; int _1; ... };
    // _1: 0 = не инициализирован, -1 = идёт, 1 = готов
    int* state = reinterpret_cast<int*>(static_cast<char*>(guard) + sizeof(int));
    const int v = __atomic_load_n(state, __ATOMIC_ACQUIRE);
    if (v != 0) {
        return 0; // готов или уже идёт инициализация
    }
    int expected = 0;
    if (__atomic_compare_exchange_n(state, &expected, -1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return -1; // нужно инициализировать
    }
    return 0; // кто-то успел
}

void br_cxa_guard_release_impl(void* guard) {
    int* state = reinterpret_cast<int*>(static_cast<char*>(guard) + sizeof(int));
    __atomic_store_n(state, 1, __ATOMIC_RELEASE);
}

// ---- std::string layout (zig libc++, aarch64 LE, SSO=23) --------------------
// short: byte0 = 1 | (size*2 << 1) = 1 | (size << 2), bytes1..23 = данные
// long:  qword0 = 1 | (cap << 1)  [63 бита], qword1 = size, qword2 = data*

constexpr size_t kSsoCap = 23;

struct BrStrRep {
    uint8_t  flags;      // short: bit0=1, bits1..7 = size*2
    char     data[23];
};

inline bool BrStrIsShort(const BrStrRep* r) { return (r->flags & 1) != 0; }
inline size_t BrStrSizeShort(const BrStrRep* r) { return (r->flags >> 2) & 0x3F; }

struct BrStrLong {
    uint64_t q0; // bit0=1, cap в битах 1..63
    uint64_t size;
    char*    data;
};

inline uint64_t BrStrCapLong(const BrStrLong* l) { return (l->q0 >> 1) & 0x7FFFFFFFFFFFFFFFULL; }

inline size_t BrStrCap(const BrStrRep* r) {
    if (BrStrIsShort(r)) {
        return kSsoCap;
    }
    return static_cast<size_t>(BrStrCapLong(reinterpret_cast<const BrStrLong*>(r)));
}

inline char* BrStrData(const BrStrRep* r) {
    if (BrStrIsShort(r)) {
        return const_cast<char*>(reinterpret_cast<const char*>(r)) + 1;
    }
    return reinterpret_cast<const BrStrLong*>(r)->data;
}

inline size_t BrStrSize(const BrStrRep* r) {
    if (BrStrIsShort(r)) {
        return BrStrSizeShort(r);
    }
    return reinterpret_cast<const BrStrLong*>(r)->size;
}

// Новый long-строка с capacity >= min_cap (чётный)
void BrStrGrow(void* self, size_t min_cap) {
    BrStrRep* r = static_cast<BrStrRep*>(self);
    const size_t old_size = BrStrSize(r);
    char* old_data = BrStrData(r);

    size_t cap = 48;
    while (cap < min_cap) {
        cap *= 2;
    }
    if (cap & 1) {
        cap += 1; // libc++ ожидает чётную ёмкость
    }

    char* p = static_cast<char*>(malloc(cap + 1));
    if (p == nullptr) {
        std::abort();
    }
    if (old_data != nullptr) {
        memcpy(p, old_data, old_size);
    }
    p[old_size] = '\0';

    if (!BrStrIsShort(r)) {
        free(reinterpret_cast<const BrStrLong*>(r)->data);
    }

    BrStrLong* l = reinterpret_cast<BrStrLong*>(r);
    l->q0 = 1 | (static_cast<uint64_t>(cap) << 1);
    l->size = old_size;
    l->data = p;
}

} // namespace

extern "C" {

// ---- __cxa_guard -------------------------------------------------------------
int __cxa_guard_acquire(void* guard) {
    return br_cxa_guard_acquire_impl(guard);
}

void __cxa_guard_release(void* guard) {
    br_cxa_guard_release_impl(guard);
}

// ---- glibc-имя sscanf: glibc-прототип vsscanf линкуется под asm-именем
//      __isoc99_vsscanf, которого НЕТ в bionic (там только vsscanf).
//      Обходим asm-label'ом: вызываем реальный bionic vsscanf.
namespace {
int br_vsscanf_real(const char* str, const char* fmt, va_list ap) __asm__("vsscanf");
}

int __isoc99_sscanf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const char* str = va_arg(ap, const char*);
    const int r = br_vsscanf_real(str, fmt, ap);
    va_end(ap);
    return r;
}

// ---- std::string (out-of-line методы zig libc++) --------------------------------
// basic_string<char, char_traits<char>, allocator<char>>::reserve(size_t)
void _ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE7reserveEm(void* self, size_t n) {
    BrStrRep* r = static_cast<BrStrRep*>(self);
    if (n > BrStrCap(r)) {
        BrStrGrow(r, n);
    }
}

// basic_string<char, char_traits<char>, allocator<char>>::push_back(char)
void _ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE9push_backEc(void* self, char c) {
    BrStrRep* r = static_cast<BrStrRep*>(self);
    const size_t sz = BrStrSize(r);
    if (BrStrIsShort(r) && sz < kSsoCap) {
        reinterpret_cast<char*>(r)[1 + sz] = c;
        r->flags = static_cast<uint8_t>(1 | ((sz + 1) << 2));
        if (sz + 1 < kSsoCap) {
            reinterpret_cast<char*>(r)[2 + sz] = '\0';
        }
        return;
    }
    BrStrGrow(r, sz + 1);
    BrStrLong* l = reinterpret_cast<BrStrLong*>(r);
    l->data[sz] = c;
    l->size = sz + 1;
    l->data[sz + 1] = '\0';
}

// basic_string<char,...>::assign(const char*)
void _ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc(void* self, const char* src) {
    if (src == nullptr) {
        src = "";
    }
    size_t n = 0;
    while (src[n] != '\0') {
        ++n;
    }
    BrStrRep* r = static_cast<BrStrRep*>(self);
    if (n > BrStrCap(r)) {
        BrStrGrow(r, n);
    }
    char* dst = BrStrData(r);
    std::memcpy(dst, src, n);
    if (BrStrIsShort(r)) {
        r->flags = static_cast<uint8_t>(1 | (n << 2));
    } else {
        reinterpret_cast<BrStrLong*>(r)->size = n;
    }
    dst[n] = '\0';
}

// std::__next_prime(size_t) — ближайшее простое >= n
size_t _ZNSt3__112__next_primeEm(size_t n) {
    if (n < 2) {
        return 2;
    }
    if (n == 2) {
        return 2;
    }
    if ((n & 1) == 0) {
        ++n;
    }
    for (;;) {
        bool prime = true;
        for (size_t d = 3; d * d <= n; d += 2) {
            if (n % d == 0) {
                prime = false;
                break;
            }
        }
        if (prime) {
            return n;
        }
        n += 2;
    }
}

// ---- std::mutex (pthread_mutex_t by value, zero-initialized) --------------------
void _ZNSt3__15mutex4lockEv(void* self) {
    pthread_mutex_lock(static_cast<pthread_mutex_t*>(self));
}

void _ZNSt3__15mutex6unlockEv(void* self) {
    pthread_mutex_unlock(static_cast<pthread_mutex_t*>(self));
}

// ---- std::thread (pthread_t by value) -------------------------------------------
// std::thread::~thread()
void _ZNSt3__16threadD1Ev(void* self) {
    pthread_t* t = static_cast<pthread_t*>(self);
    if (*t != 0) {
        // joinable-поток при деструкции -> terminate (как в libc++)
        std::fprintf(stderr, "[rt_compat] std::thread: terminate (joinable на деструкции)\n");
        std::abort();
    }
    (void)t;
}

// std::thread::detach()
void _ZNSt3__16thread6detachEv(void* self) {
    pthread_t* t = static_cast<pthread_t*>(self);
    if (*t != 0) {
        pthread_detach(*t);
        *t = 0;
    }
}

// std::__thread_struct ctor/dtor — в нашем пути (thread без аргументов)
// imp не используется: trampoline лишь сохраняет указатель в TLS.
void _ZNSt3__115__thread_structC1Ev(void* self) {
    *static_cast<void**>(self) = nullptr;
}

void _ZNSt3__115__thread_structD1Ev(void* self) {
    void** p = static_cast<void**>(self);
    // __p_ у нас всегда nullptr (см. комментарий в шапке)
    *p = nullptr;
}

// std::__thread_local_data() -> __thread_specific_ptr<__thread_struct>&
// Вложенный set_pointer (inline) запишет указатель в первые 8 байт объекта.
void* _ZNSt3__119__thread_local_dataEv(void) {
    static __thread uintptr_t g_tls_slot = 0;
    return &g_tls_slot;
}

// std::this_thread::sleep_for(const duration<int64_t, ratio<1,1000000000>>& ns)
void _ZNSt3__111this_thread9sleep_forERKNS_6chrono8durationIxNS_5ratioILl1ELl1000000000EEEEE(
    const int64_t* ns) {
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(*ns / 1000000000LL);
    ts.tv_nsec = static_cast<long>(*ns % 1000000000LL);
    while (nanosleep(&ts, &ts) == -1) {
        // EINTR -> продолжаем
    }
}

// std::chrono::steady_clock::now() -> time_point = duration<int64_t, ns> (в x0)
int64_t _ZNSt3__16chrono12steady_clock3nowEv(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

// libc++ helper'ы -----------------------------------------------------------------
// std::__throw_system_error(int, const char*) — без исключений: abort
void _ZNSt3__120__throw_system_errorEiPKc(int, const char*) {
    std::fprintf(stderr, "[rt_compat] __throw_system_error\n");
    std::abort();
}

// std::__libcpp_verbose_abort(const char*, ...)
void _ZNSt3__122__libcpp_verbose_abortEPKcz(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
    va_end(ap);
    std::fprintf(stderr, "[rt_compat] %s\n", buf);
    std::abort();
}

} // extern "C"
