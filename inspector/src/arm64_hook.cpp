#include "arm64_hook.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define BR_HOOK_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "BRHook", fmt, ##__VA_ARGS__)
#define BR_HOOK_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "BRHook", fmt, ##__VA_ARGS__)
#define BR_HOOK_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "BRHook", fmt, ##__VA_ARGS__)
#else
#define BR_HOOK_LOGI(fmt, ...) std::fprintf(stdout, "[BRHook][INFO] " fmt "\n", ##__VA_ARGS__)
#define BR_HOOK_LOGW(fmt, ...) std::fprintf(stdout, "[BRHook][WARN] " fmt "\n", ##__VA_ARGS__)
#define BR_HOOK_LOGE(fmt, ...) std::fprintf(stderr, "[BRHook][ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

namespace br::hook {

namespace {
ExternalHookBackendFn   g_external_backend = nullptr;
std::mutex              g_hook_mutex;
std::vector<HookRecord> g_hooks;
const char*             g_tramp_file_hint = nullptr;

// Кодирование 64-битной константы в регистр Xd (0..30) через MOVZ + до трёх MOVK
void EmitLoadImm64(uint32_t rd, uint64_t value, std::vector<uint32_t>& out) {
    // MOVZ Xd, #imm16, LSL #0  -> 0xD2800000 | (imm16 << 5) | rd
    const uint32_t hw0 = static_cast<uint32_t>(value & 0xFFFFULL);
    out.push_back(0xD2800000U | (hw0 << 5) | (rd & 0x1FU));

    for (unsigned shift_idx = 1; shift_idx <= 3; ++shift_idx) {
        const uint32_t hw = static_cast<uint32_t>((value >> (shift_idx * 16)) & 0xFFFFULL);
        if (hw != 0) {
            // MOVK Xd, #imm16, LSL #(shift_idx*16) -> 0xF2800000 | (hw_shift << 21) | (imm16 << 5) | rd
            out.push_back(0xF2800000U | (shift_idx << 21) | (hw << 5) | (rd & 0x1FU));
        }
    }
}

[[maybe_unused]] void FlushInstructionCache(void* begin, size_t len) noexcept {
#if defined(__aarch64__)
    char* start = static_cast<char*>(begin);
    __builtin___clear_cache(start, start + len);
#else
    (void)begin;
    (void)len;
#endif
}
} // namespace

void Arm64Hook::SetExternalBackend(ExternalHookBackendFn backend_fn) noexcept {
    g_external_backend = backend_fn;
}

void Arm64Hook::SetTrampolineFileHint(const char* path) noexcept {
    g_tramp_file_hint = path ? path : nullptr;
}

// File-backed RWX-маппинг: RW-маппинг хвоста первого exec-сегмента подсказанного
// ELF, затем mprotect RX. Обходит запрет анонимного execmem (SELinux), если
// device разрешает file-backed exec.
void* AllocateTrampolineFileBacked(const void* tramp_words, size_t tramp_bytes,
                                               size_t alloc_size, size_t page_size) {
    if (!g_tramp_file_hint) {
        return nullptr;
    }
    FILE* fp = std::fopen(g_tramp_file_hint, "rb");
    if (!fp) {
        BR_HOOK_LOGE("tramp file-backed: fopen(%s) не удался", g_tramp_file_hint);
        return nullptr;
    }

    struct Ehdr64 {
        unsigned char e_ident[16];
        uint16_t e_type, e_machine;
        uint32_t e_version;
        uint64_t e_entry, e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize, e_phentsize, e_phnum;
        uint16_t e_shentsize, e_shnum, e_shstrndx;
    } eh{};
    size_t exec_off = 0;
    uint64_t exec_filesz = 0;

    if (std::fread(&eh, 1, sizeof(eh), fp) == sizeof(eh) &&
        std::memcmp(eh.e_ident, "\x7f" "ELF", 4) == 0 &&
        eh.e_phnum > 0 && eh.e_phnum <= 128 &&
        std::fseek(fp, (long)eh.e_phoff, SEEK_SET) == 0) {
        struct Phdr64 {
            uint32_t p_type, p_flags;
            uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
        } ph{};
        for (uint16_t i = 0; i < eh.e_phnum; ++i) {
            if (std::fread(&ph, 1, sizeof(ph), fp) != sizeof(ph)) break;
            if (ph.p_type == 1 && (ph.p_flags & 1) && ph.p_filesz >= page_size) {
                exec_off = (size_t)ph.p_offset;
                exec_filesz = ph.p_filesz;
                break;
            }
        }
    }
    std::fclose(fp);

    if (exec_off == 0) {
        BR_HOOK_LOGE("tramp file-backed: exec-сегмент не найден в %s", g_tramp_file_hint);
        return nullptr;
    }

    const size_t map_size = (alloc_size + page_size - 1) & ~(page_size - 1);
    // Берём хвост exec-сегмента: MAP_PRIVATE не трогает файл до записи,
    // а запись уйдёт в copy-on-write-страницы, оригинал цел
    const size_t seg_tail = (exec_filesz >= (map_size + page_size)) ? (exec_filesz - map_size) : 0;

    // Открыть fd заново под mmap
    FILE* fp2 = std::fopen(g_tramp_file_hint, "rb");
    if (!fp2) {
        return nullptr;
    }
    const int fd = fileno(fp2);
    std::fclose(fp2);
    if (fd < 0) {
        return nullptr;
    }

    // RW-маппинг (без exec) — всегда разрешён для своих файлов
    void* p = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE, fd, (off_t)(exec_off + seg_tail));
    if (p == MAP_FAILED) {
        BR_HOOK_LOGE("tramp file-backed: mmap RW failed errno=%d", errno);
        return nullptr;
    }
    // Пишем трэмплин пока RW, затем RX
    std::memcpy(p, tramp_words, tramp_bytes);
    FlushInstructionCache(p, tramp_bytes);
    if (::mprotect(p, map_size, PROT_READ | PROT_EXEC) != 0) {
        BR_HOOK_LOGE("tramp file-backed: mprotect RX failed errno=%d", errno);
        ::munmap(p, map_size);
        return nullptr;
    }
    BR_HOOK_LOGI("tramp file-backed OK: %s @ %p (map_size=%zu)", g_tramp_file_hint, p, map_size);
    return p;
}

void Arm64Hook::EncodeAbsoluteJumpX17(uintptr_t dest_addr, uint32_t out_words[4]) noexcept {
    out_words[0] = 0x58000051U; // ldr x17, #8
    out_words[1] = 0xD61F0220U; // br  x17
    const uint64_t addr64 = static_cast<uint64_t>(dest_addr);
    out_words[2] = static_cast<uint32_t>(addr64 & 0xFFFFFFFFULL);
    out_words[3] = static_cast<uint32_t>((addr64 >> 32) & 0xFFFFFFFFULL);
}

bool Arm64Hook::BuildRelocatedTrampoline(const uint32_t orig_insns[4],
                                         uintptr_t orig_pc,
                                         std::vector<uint32_t>& out_words) {
    out_words.clear();
    out_words.reserve(24);

    for (size_t i = 0; i < 4; ++i) {
        const uint32_t insn = orig_insns[i];
        const uint64_t pc   = static_cast<uint64_t>(orig_pc) + i * 4ULL;

        // 1. ADR / ADRP: (insn & 0x1F000000) == 0x10000000
        if ((insn & 0x1F000000U) == 0x10000000U) {
            const bool     is_adrp = (insn & 0x80000000U) != 0;
            const uint32_t rd      = insn & 0x1FU;
            const uint32_t immlo   = (insn >> 29) & 0x3U;
            const uint32_t immhi   = (insn >> 5) & 0x7FFFFU;
            int64_t        imm21   = static_cast<int64_t>((immhi << 2) | immlo);
            if (imm21 & (1LL << 20)) {
                imm21 -= (1LL << 21); // знаковое расширение 21 бит
            }
            uint64_t resolved = 0;
            if (is_adrp) {
                resolved = (pc & ~0xFFFULL) + static_cast<uint64_t>(imm21 * 4096LL);
            } else {
                resolved = pc + static_cast<uint64_t>(imm21);
            }
            EmitLoadImm64(rd, resolved, out_words);
            continue;
        }

        // 2. Безусловные B / BL: (insn & 0x7C000000) == 0x14000000
        if ((insn & 0x7C000000U) == 0x14000000U) {
            const bool is_bl = (insn & 0x80000000U) != 0;
            int64_t    imm26 = static_cast<int64_t>(insn & 0x03FFFFFFU);
            if (imm26 & (1LL << 25)) {
                imm26 -= (1LL << 26);
            }
            const uint64_t target = pc + static_cast<uint64_t>(imm26 * 4LL);
            if (is_bl) {
                EmitLoadImm64(17, target, out_words);
                out_words.push_back(0xD63F0220U); // blr x17
            } else {
                uint32_t jmp[4]{};
                EncodeAbsoluteJumpX17(static_cast<uintptr_t>(target), jmp);
                out_words.insert(out_words.end(), jmp, jmp + 4);
            }
            continue;
        }

        // 3. Защита от коротких PC-зависимых условных переходов (B.cond, CBZ/CBNZ, TBZ/TBNZ, LDR literal)
        if (((insn & 0xFF000010U) == 0x54000000U) || // B.cond
            ((insn & 0x7E000000U) == 0x34000000U) || // CBZ / CBNZ
            ((insn & 0x7E000000U) == 0x36000000U) || // TBZ / TBNZ
            ((insn & 0x3B000000U) == 0x18000000U))   // LDR literal
        {
            return false; // небезопасный пролог для 16-байтового перехвата без расширенной релокации
        }

        // Обычная позиционно-независимая инструкция (STP, LDP, STR, LDR imm, MOV, ADD, SUB...)
        out_words.push_back(insn);
    }

    // Возврат в оригинальную функцию на orig_pc + 16
    uint32_t back_jmp[4]{};
    EncodeAbsoluteJumpX17(orig_pc + 16ULL, back_jmp);
    out_words.insert(out_words.end(), back_jmp, back_jmp + 4);
    return true;
}

bool Arm64Hook::Install(void* target, void* detour, void** out_original) {
    if (!target || !detour || !out_original) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_hook_mutex);

    if (g_external_backend) {
        if (g_external_backend(target, detour, out_original)) {
            HookRecord rec{};
            rec.target_addr   = reinterpret_cast<uintptr_t>(target);
            rec.detour_addr   = reinterpret_cast<uintptr_t>(detour);
            rec.trampoline_va = reinterpret_cast<uintptr_t>(*out_original);
            rec.active        = true;
            rec.used_external = true;
            g_hooks.push_back(rec);
            return true;
        }
        return false;
    }

#if !defined(__aarch64__)
    return false;
#else
    const uintptr_t target_va = reinterpret_cast<uintptr_t>(target);
    uint32_t orig_insns[4]{};
    std::memcpy(orig_insns, target, sizeof(orig_insns));

    std::vector<uint32_t> tramp_words;
    if (!BuildRelocatedTrampoline(orig_insns, target_va, tramp_words)) {
        // Диагностика: какие инструкции в прологе и почему билдер отказал
        char insns[128] = {};
        for (int i = 0; i < 4; ++i) {
            std::snprintf(insns + i * 10, sizeof(insns) - i * 10, "%s%08x", i ? " " : "", orig_insns[i]);
        }
        int bad_idx = -1;
        for (int i = 0; i < 4; ++i) {
            const uint32_t insn = orig_insns[i];
            if (((insn & 0xFF000010U) == 0x54000000U) ||
                ((insn & 0x7E000000U) == 0x34000000U) ||
                ((insn & 0x7E000000U) == 0x36000000U) ||
                ((insn & 0x3B000000U) == 0x18000000U)) {
                bad_idx = i;
                break;
            }
        }
        BR_HOOK_LOGE("Trampoline builder rejected @ 0x%lx: insns=[%s] bad_idx=%d "
                     "(B.cond/CBZ/TBZ/LDR-literal запрещены в 16-байтовом прологе)",
                     static_cast<unsigned long>(target_va), insns, bad_idx);
        return false;
    }

    const size_t tramp_bytes = tramp_words.size() * sizeof(uint32_t);
    const size_t page_size   = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    const size_t alloc_size  = (tramp_bytes + page_size - 1) & ~(page_size - 1);

    // Вариант 1: анонимный RWX
    void* tramp_mem = ::mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp_mem != MAP_FAILED) {
        std::memcpy(tramp_mem, tramp_words.data(), tramp_bytes);
        FlushInstructionCache(tramp_mem, tramp_bytes);
        if (::mprotect(tramp_mem, alloc_size, PROT_READ | PROT_EXEC) == 0) {
            BR_HOOK_LOGI("Trampoline anon-RWX OK: 0x%lx -> %p (%zu B) [hint=%s]",
                         static_cast<unsigned long>(target_va), tramp_mem, alloc_size,
                         g_tramp_file_hint ? g_tramp_file_hint : "-");
        } else {
            BR_HOOK_LOGE("Trampoline anon: mprotect RX failed errno=%d — пробую file-backed", errno);
            ::munmap(tramp_mem, alloc_size);
            tramp_mem = nullptr;
        }
    } else {
        BR_HOOK_LOGE("Trampoline anon: mmap failed errno=%d — пробую file-backed", errno);
    }

    // Вариант 2: file-backed (RW-маппинг exec-сегмента своего файла -> RX)
    if (tramp_mem == nullptr) {
        tramp_mem = AllocateTrampolineFileBacked(tramp_words.data(), tramp_bytes,
                                                 alloc_size, page_size);
    }
    if (tramp_mem == nullptr) {
        BR_HOOK_LOGE("Trampoline: оба варианта RWX провалились (anon и file-backed)");
        return false;
    }

    // Открываем страницу(ы) целевой функции на запись патча (учитываем пересечение границы страницы)
    const uintptr_t page_start = target_va & ~(static_cast<uintptr_t>(page_size) - 1ULL);
    const uintptr_t page_end   = (target_va + 16ULL + page_size - 1ULL) & ~(static_cast<uintptr_t>(page_size) - 1ULL);
    const size_t    patch_span = static_cast<size_t>(page_end - page_start);

    if (::mprotect(reinterpret_cast<void*>(page_start), patch_span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        BR_HOOK_LOGE("mprotect(target_page=0x%lx, span=%zu, RWX) failed errno=%d",
                     static_cast<unsigned long>(page_start), patch_span, errno);
        ::munmap(tramp_mem, alloc_size);
        return false;
    }

    uint32_t patch_words[4]{};
    EncodeAbsoluteJumpX17(reinterpret_cast<uintptr_t>(detour), patch_words);
    std::memcpy(target, patch_words, sizeof(patch_words));
    FlushInstructionCache(target, sizeof(patch_words));
    ::mprotect(reinterpret_cast<void*>(page_start), patch_span, PROT_READ | PROT_EXEC);

    *out_original = tramp_mem;

    HookRecord rec{};
    rec.target_addr    = target_va;
    rec.detour_addr    = reinterpret_cast<uintptr_t>(detour);
    rec.trampoline_va  = reinterpret_cast<uintptr_t>(tramp_mem);
    rec.trampoline_len = alloc_size;
    std::memcpy(rec.saved_insns, orig_insns, sizeof(orig_insns));
    rec.active         = true;
    rec.used_external  = false;
    g_hooks.push_back(rec);
    return true;
#endif
}

void Arm64Hook::RemoveAll() noexcept {
    std::lock_guard<std::mutex> lock(g_hook_mutex);
#if defined(__aarch64__)
    const size_t page_size = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    for (auto it = g_hooks.rbegin(); it != g_hooks.rend(); ++it) {
        if (!it->active || it->used_external) {
            continue;
        }
        const uintptr_t page_start = it->target_addr & ~(static_cast<uintptr_t>(page_size) - 1ULL);
        const uintptr_t page_end   = (it->target_addr + 16ULL + page_size - 1ULL) & ~(static_cast<uintptr_t>(page_size) - 1ULL);
        const size_t    patch_span = static_cast<size_t>(page_end - page_start);

        if (::mprotect(reinterpret_cast<void*>(page_start), patch_span, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            std::memcpy(reinterpret_cast<void*>(it->target_addr), it->saved_insns, sizeof(it->saved_insns));
            FlushInstructionCache(reinterpret_cast<void*>(it->target_addr), sizeof(it->saved_insns));
            ::mprotect(reinterpret_cast<void*>(page_start), patch_span, PROT_READ | PROT_EXEC);
        }
        it->active = false;
    }
#endif
}

const std::vector<HookRecord>& Arm64Hook::GetInstalledHooks() noexcept {
    return g_hooks;
}

} // namespace br::hook
