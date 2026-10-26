// ============================================================================
// got_hook.cpp — реализация GOT-хука (см. got_hook.hpp).
// ============================================================================

#include "got_hook.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "safe_memory.hpp"

// Зеркало SafeMemory (process_vm_writev) — объявление из mod_features
namespace br::mem {
bool WriteBytes(uintptr_t addr, const void* in_buf, size_t size) noexcept;
}

#if defined(__ANDROID__)
#include <android/log.h>
#define BR_GOT_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "BRGOT", fmt, ##__VA_ARGS__)
#define BR_GOT_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "BRGOT", fmt, ##__VA_ARGS__)
#define BR_GOT_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "BRGOT", fmt, ##__VA_ARGS__)
#else
#define BR_GOT_LOGI(fmt, ...) std::fprintf(stdout, "[BRGOT][INFO] " fmt "\n", ##__VA_ARGS__)
#define BR_GOT_LOGW(fmt, ...) std::fprintf(stdout, "[BRGOT][WARN] " fmt "\n", ##__VA_ARGS__)
#define BR_GOT_LOGE(fmt, ...) std::fprintf(stderr, "[BRGOT][ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

namespace br::hook {

namespace {

// ---- Минимальные ELF64-типы (то, что нужно для парсинга) -------------------
struct Ehdr {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
};

struct Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct Dyn {
    int64_t  d_tag;
    uint64_t d_val;
};

struct Sym {
    uint32_t      st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
    uint64_t      st_value;
    uint64_t      st_size;
};

struct Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

constexpr uint64_t kPTDynamic = 2;
constexpr uint64_t kPF_X      = 1;
constexpr int64_t  kDTSTRTAB  = 5;
constexpr int64_t  kDTSYMTAB  = 6;
constexpr int64_t  kDTPLTREL  = 24;
constexpr int64_t  kDTJMPREL  = 23;
constexpr int64_t  kDTPLTRELSZ = 2;
constexpr int64_t  kDTSTRTABSZ = 11;
constexpr uint32_t kRAarch64JumpSlot = 2; // R_AARCH64_JUMP_SLOT

std::vector<GotHookRecord> g_got_hooks;

} // namespace

uintptr_t FindFirstExecSegmentFileOffset(const char* path, size_t* out_size) {
    if (!path || !*path || !out_size) {
        return 0;
    }
    *out_size = 0;

    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    Ehdr eh{};
    const size_t rd = std::fread(&eh, 1, sizeof(eh), fp);
    (void)rd;
    if (rd != sizeof(eh) || std::memcmp(eh.e_ident, "\x7f" "ELF", 4) != 0 ||
        eh.e_ident[4] != 2 /*ELFCLASS64*/ || eh.e_phnum == 0 || eh.e_phnum > 128) {
        std::fclose(fp);
        return 0;
    }

    if (std::fseek(fp, static_cast<long>(eh.e_phoff), SEEK_SET) != 0) {
        std::fclose(fp);
        return 0;
    }

    uintptr_t off = 0;
    size_t    sz  = 0;
    Phdr      ph{};
    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        if (std::fread(&ph, 1, sizeof(ph), fp) != sizeof(ph)) {
            break;
        }
        // PT_LOAD = 1: берём первый исполняемый сегмент
        if (ph.p_type == 1 && (ph.p_flags & kPF_X) && ph.p_filesz >= 4096) {
            off = static_cast<uintptr_t>(ph.p_offset);
            sz  = static_cast<size_t>(ph.p_filesz);
            break;
        }
    }
    std::fclose(fp);

    if (off != 0 && sz != 0) {
        *out_size = sz;
    }
    return off;
}

bool InstallGotHook(uintptr_t module_base, const char* symbol, uintptr_t detour,
                    uintptr_t* out_original) {
    using br::mem::SafeMemory;

    if (!module_base || !symbol || detour == 0) {
        return false;
    }

    Ehdr eh{};
    if (!SafeMemory::ReadBytes(module_base, &eh, sizeof(eh)) ||
        std::memcmp(eh.e_ident, "\x7f" "ELF", 4) != 0 || eh.e_ident[4] != 2 ||
        eh.e_phnum == 0 || eh.e_phnum > 128 ||
        (module_base + eh.e_phoff) < module_base) {
        BR_GOT_LOGE("ELF-заголовок %s не прочитан", symbol);
        return false;
    }

    // PT_DYNAMIC
    uintptr_t dyn_va = 0;
    Phdr ph{};
    const size_t phentsize = eh.e_phentsize ? eh.e_phentsize : sizeof(Phdr);
    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        const uintptr_t ph_addr = module_base + eh.e_phoff + static_cast<uintptr_t>(i) * phentsize;
        if (!SafeMemory::ReadBytes(ph_addr, &ph, sizeof(Phdr))) {
            break;
        }
        if (ph.p_type == kPTDynamic) {
            dyn_va = module_base + ph.p_vaddr;
            break;
        }
    }
    if (dyn_va == 0) {
        BR_GOT_LOGE("PT_DYNAMIC не найден для %s", symbol);
        return false;
    }

    uintptr_t symtab_va = 0, strtab_va = 0, jmprel_va = 0, pltrelsz = 0;
    int64_t   pltrel    = 0;
    Dyn       d{};
    for (int i = 0; i < 256; ++i) {
        if (!SafeMemory::ReadBytes(dyn_va + static_cast<uintptr_t>(i) * sizeof(Dyn), &d, sizeof(d))) {
            break;
        }
        if (d.d_tag == 0) {
            break;
        }
        switch (d.d_tag) {
            case kDTSYMTAB:   symtab_va = module_base + d.d_val; break;
            case kDTSTRTAB:   strtab_va = module_base + d.d_val; break;
            case kDTJMPREL:   jmprel_va = module_base + d.d_val; break;
            case kDTPLTRELSZ: pltrelsz  = d.d_val; break;
            case kDTPLTREL:   pltrel    = static_cast<int64_t>(d.d_val); break;
            default: break;
        }
    }

    if (symtab_va == 0 || strtab_va == 0 || jmprel_va == 0 || pltrelsz == 0) {
        BR_GOT_LOGE("Динамическая таблица %s неполная (symtab=%lx strtab=%lx jmprel=%lx sz=%lu)",
                    symbol,
                    static_cast<unsigned long>(symtab_va),
                    static_cast<unsigned long>(strtab_va),
                    static_cast<unsigned long>(jmprel_va),
                    static_cast<unsigned long>(pltrelsz));
        return false;
    }
    // DT_PLTREL == DT_JMPREL (23) => записи в формате Rela
    if (pltrel != 0 && pltrel != kDTJMPREL) {
        BR_GOT_LOGE("Неожиданный DT_PLTREL=%ld для %s", static_cast<long>(pltrel), symbol);
        return false;
    }

    const size_t rela_count = pltrelsz / sizeof(Rela);
    if (rela_count == 0 || rela_count > 200000) {
        BR_GOT_LOGE("Аномальный DT_PLTRELSZ=%lu для %s",
                    static_cast<unsigned long>(pltrelsz), symbol);
        return false;
    }

    BR_GOT_LOGI("Поиск %s: relas=%zu jmprel=0x%lx strtab=0x%lx",
                symbol, rela_count,
                static_cast<unsigned long>(jmprel_va),
                static_cast<unsigned long>(strtab_va));

    uintptr_t got_slot = 0;
    uintptr_t saved    = 0;

    Rela      rela{};
    Sym       sym{};
    char      name[128] = {};
    for (size_t i = 0; i < rela_count; ++i) {
        if (!SafeMemory::ReadBytes(jmprel_va + i * sizeof(Rela), &rela, sizeof(rela))) {
            break;
        }
        if ((rela.r_info & 0xFFFFFFFFULL) != kRAarch64JumpSlot) {
            continue;
        }
        const uint32_t idx = static_cast<uint32_t>(rela.r_info >> 32);
        if (idx == 0) {
            continue;
        }
        if (!SafeMemory::ReadBytes(symtab_va + static_cast<uintptr_t>(idx) * sizeof(Sym), &sym, sizeof(sym))) {
            continue;
        }
        std::memset(name, 0, sizeof(name));
        if (!SafeMemory::ReadBytes(strtab_va + sym.st_name, name, sizeof(name) - 1)) {
            continue;
        }
        if (std::strcmp(name, symbol) != 0) {
            continue;
        }
        got_slot = module_base + rela.r_offset;
        if (!SafeMemory::ReadBytes(got_slot, &saved, sizeof(saved))) {
            BR_GOT_LOGE("Чтение GOT-слота 0x%lx для %s не удалось",
                        static_cast<unsigned long>(got_slot), symbol);
            return false;
        }
        break;
    }

    if (got_slot == 0) {
        BR_GOT_LOGE("Слот PLT/GOT для %s не найден (вызовов через PLT нет?)", symbol);
        return false;
    }
    if (saved == 0) {
        BR_GOT_LOGW("GOT-слот %s пуст (0) — возможно, символ ещё не резолвится", symbol);
    }

    if (!br::mem::WriteBytes(got_slot, &detour, sizeof(detour))) {
        BR_GOT_LOGE("Запись GOT-слота 0x%lx (%s) не удалась",
                    static_cast<unsigned long>(got_slot), symbol);
        return false;
    }

    if (out_original) {
        *out_original = saved;
    }

    GotHookRecord rec{};
    rec.got_slot_va = got_slot;
    rec.saved_value = saved;
    rec.active      = true;
    rec.symbol      = symbol;
    g_got_hooks.push_back(rec);

    BR_GOT_LOGI("GOT-хук %s: slot=0x%lx detour=0x%lx orig=0x%lx",
                symbol,
                static_cast<unsigned long>(got_slot),
                static_cast<unsigned long>(detour),
                static_cast<unsigned long>(saved));
    return true;
}

bool RestoreGotHooks() {
    bool all_ok = true;
    for (auto& rec : g_got_hooks) {
        if (!rec.active) {
            continue;
        }
        if (!br::mem::WriteBytes(rec.got_slot_va, &rec.saved_value, sizeof(rec.saved_value))) {
            BR_GOT_LOGE("Восстановление GOT-хука %s не удалось", rec.symbol.c_str());
            all_ok = false;
            continue;
        }
        rec.active = false;
        BR_GOT_LOGI("GOT-хук %s снят (slot=0x%lx)", rec.symbol.c_str(),
                    static_cast<unsigned long>(rec.got_slot_va));
    }
    return all_ok;
}

const std::vector<GotHookRecord>& GetGotHooks() {
    return g_got_hooks;
}

} // namespace br::hook
