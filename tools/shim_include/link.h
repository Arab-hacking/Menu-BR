#pragma once
// ============================================================================
// link.h — минимальный shim для кросс-компиляции zig cc.
// Используется internal_imgui_bridge.cpp (dl_iterate_phdr для поиска
// базы libblackrussia-client.so). dl_iterate_phdr экспортируется
// bionic libc — резолвится в рантайме.
// Типы elf_phdr описаны сами (glibc elf.h отдаёт их только под __USE_MISC).
// ============================================================================

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__) || defined(__x86_64__) || (defined(__WORDSIZE) && __WORDSIZE == 64)
typedef uint64_t br_shim_elf_addr;
typedef uint16_t br_shim_elf_half;
struct br_shim_elf_phdr {
    uint32_t  p_type;
    uint32_t  p_flags;
    uint64_t  p_offset;
    uint64_t  p_vaddr;
    uint64_t  p_paddr;
    uint64_t  p_filesz;
    uint64_t  p_memsz;
    uint64_t  p_align;
};
#else
typedef uint32_t br_shim_elf_addr;
typedef uint16_t br_shim_elf_half;
struct br_shim_elf_phdr {
    uint32_t  p_type;
    uint32_t  p_offset;
    uint32_t  p_vaddr;
    uint32_t  p_paddr;
    uint32_t  p_filesz;
    uint32_t  p_memsz;
    uint32_t  p_flags;
    uint32_t  p_align;
};
#endif

struct dl_phdr_info {
    br_shim_elf_addr       dlpi_addr;
    const char*            dlpi_name;
    const struct br_shim_elf_phdr* dlpi_phdr;
    br_shim_elf_half       dlpi_phnum;
};

typedef int (*dl_phdr_callback_t)(struct dl_phdr_info* info, size_t size, void* data);

int dl_iterate_phdr(dl_phdr_callback_t callback, void* data);

#ifdef __cplusplus
}
#endif
