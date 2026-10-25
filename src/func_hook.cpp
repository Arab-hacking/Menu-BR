/*
 * func_hook.cpp — реализация (см. func_hook.h)
 */
#include "br_common.h"
#include "func_hook.h"
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define FHOOK_PAGE() ((size_t)sysconf(_SC_PAGESIZE))

static br_fhook_t g_fhooks[BR_FHOOK_MAX];
static int        g_n_fhooks = 0;

static void flush_icache(void *begin, size_t len)
{
#if defined(__aarch64__)
    __builtin___clear_cache((char *)begin, (char *)begin + len);
#else
    (void)begin; (void)len;
#endif
}

/* MOVZ + до 3 MOVK: 64-битное значение в Xd (0..30) */
static int emit_load_imm64(uint32_t rd, uint64_t value, uint32_t *out, int max)
{
    int n = 0;
    uint32_t hw0 = (uint32_t)(value & 0xFFFF);
    if (n < max) out[n++] = 0xD2800000u | (hw0 << 5) | (rd & 0x1Fu);
    for (int si = 1; si <= 3; si++) {
        uint32_t hw = (uint32_t)((value >> (si * 16)) & 0xFFFF);
        if (hw) {
            if (n >= max) return -1;
            out[n++] = 0xF2800000u | ((uint32_t)si << 21) | (hw << 5) | (rd & 0x1Fu);
        }
    }
    return n;
}

/* ldr x17, #8; br x17; imm64 — абсолютный прыжок */
static void emit_abs_jump(uint64_t dest, uint32_t out[4])
{
    out[0] = 0x58000051u;
    out[1] = 0xD61F0220u;
    out[2] = (uint32_t)(dest & 0xFFFFFFFFu);
    out[3] = (uint32_t)((dest >> 32) & 0xFFFFFFFFu);
}

#define TRAMP_MAX_WORDS 28

/* Переносим первые 4 инструкции в trampoline c релокацией.
 * 0 = успех; -1 = небезопасный пролог. */
static int build_tramp(const uint32_t orig[4], uint64_t orig_pc,
                       uint32_t *out, int *out_n)
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t insn = orig[i];
        uint64_t pc = orig_pc + (uint64_t)i * 4;
        uint32_t tmp[8];
        int tn = 0;

        /* ADR / ADRP */
        if ((insn & 0x1F000000u) == 0x10000000u) {
            bool is_adrp = (insn & 0x80000000u) != 0;
            uint32_t rd = insn & 0x1Fu;
            int64_t imm21 = (int64_t)(((insn >> 5) & 0x7FFFFu) << 2 | (insn >> 29) & 0x3u);
            if (imm21 & (1 << 20)) imm21 -= (1 << 21);
            uint64_t resolved = is_adrp
                ? (pc & ~0xFFFu) + (uint64_t)imm21 * 4096u
                : pc + (uint64_t)imm21;
            tn = emit_load_imm64(rd, resolved, tmp, 8);
        }
        /* B / BL */
        else if ((insn & 0x7C000000u) == 0x14000000u) {
            bool is_bl = (insn & 0x80000000u) != 0;
            int64_t imm26 = (int64_t)(insn & 0x03FFFFFFu);
            if (imm26 & (1 << 25)) imm26 -= (1 << 26);
            uint64_t target = pc + (uint64_t)imm26 * 4u;
            if (is_bl) {
                tn = emit_load_imm64(17, target, tmp, 7);
                if (tn > 0) tmp[tn++] = 0xD63F0220u; /* blr x17 */
            } else {
                emit_abs_jump(target, tmp);
                tn = 4;
            }
        }
        /* PC-зависимые условные переходы и LDR literal — небезопасно */
        else if (((insn & 0xFF000010u) == 0x54000000u) ||
                 ((insn & 0x7E000000u) == 0x34000000u) ||
                 ((insn & 0x7E000000u) == 0x36000000u) ||
                 ((insn & 0x3B000000u) == 0x18000000u)) {
            return -1;
        }
        else {
            tmp[0] = insn;
            tn = 1;
        }

        if (tn < 0 || n + tn > TRAMP_MAX_WORDS) return -1;
        for (int k = 0; k < tn; k++) out[n + k] = tmp[k];
        n += tn;
    }
    uint32_t jump[4];
    emit_abs_jump(orig_pc + 16, jump);
    if (n + 4 > TRAMP_MAX_WORDS) return -1;
    for (int k = 0; k < 4; k++) out[n + k] = jump[k];
    n += 4;
    *out_n = n;
    return 0;
}

int br_fhook_install(void *target, void *detour, void **out_orig)
{
    if (!target || !detour || !out_orig) return -1;
    *out_orig = 0;

#if !defined(__aarch64__)
    (void)target; (void)detour;
    return -1; /* host: нет */
#else
    for (int i = 0; i < BR_FHOOK_MAX; i++)
        if (g_fhooks[i].active && g_fhooks[i].target == (uintptr_t)target)
            return -1; /* уже стоит */

    uintptr_t tva = (uintptr_t)target;
    uint32_t orig[4];
    memcpy(orig, target, 16);

    uint32_t words[TRAMP_MAX_WORDS];
    int nwords = 0;
    if (build_tramp(orig, tva, words, &nwords) != 0) {
        LOGE("fhook: небезопасный пролог @ %p (нужен расширенный перехват)", target);
        return -1;
    }
    size_t page = FHOOK_PAGE();
    size_t tramp_bytes = (size_t)nwords * 4;
    size_t alloc = (tramp_bytes + page - 1) & ~(page - 1);

    void *tramp = mmap(0, alloc, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) { LOGE("fhook: mmap tramp"); return -1; }
    memcpy(tramp, words, tramp_bytes);
    flush_icache(tramp, tramp_bytes);
    if (mprotect(tramp, alloc, PROT_READ | PROT_EXEC) != 0) {
        munmap(tramp, alloc);
        LOGE("fhook: mprotect tramp");
        return -1;
    }

    uintptr_t ps = tva & ~(page - 1);
    uintptr_t pe = (tva + 16 + page - 1) & ~(page - 1);
    size_t span = (size_t)(pe - ps);
    if (mprotect((void *)ps, span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        munmap(tramp, alloc);
        LOGE("fhook: mprotect target %p", target);
        return -1;
    }
    uint32_t patch[4];
    emit_abs_jump((uintptr_t)detour, patch);
    memcpy(target, patch, 16);
    flush_icache(target, 16);
    mprotect((void *)ps, span, PROT_READ | PROT_EXEC);

    int slot = -1;
    for (int i = 0; i < BR_FHOOK_MAX; i++)
        if (!g_fhooks[i].active) { slot = i; break; }
    if (slot < 0) { LOGE("fhook: нет слотов"); return -1; }
    g_fhooks[slot].target = tva;
    g_fhooks[slot].detour = (uintptr_t)detour;
    g_fhooks[slot].trampoline = (uintptr_t)tramp;
    memcpy(g_fhooks[slot].saved, orig, 16);
    g_fhooks[slot].tramp_size = alloc;
    g_fhooks[slot].active = 1;
    if (slot >= g_n_fhooks) g_n_fhooks = slot + 1;

    *out_orig = tramp;
    LOGI("fhook: установлен target=%p detour=%p tramp=%p",
         target, detour, tramp);
    return 0;
#endif
}

void br_fhook_remove_all(void)
{
#if !defined(__aarch64__)
    return;
#else
    size_t page = FHOOK_PAGE();
    for (int i = 0; i < BR_FHOOK_MAX; i++) {
        br_fhook_t *h = &g_fhooks[i];
        if (!h->active) continue;
        uintptr_t ps = h->target & ~(page - 1);
        uintptr_t pe = (h->target + 16 + page - 1) & ~(page - 1);
        size_t span = (size_t)(pe - ps);
        if (mprotect((void *)ps, span, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            memcpy((void *)h->target, h->saved, 16);
            flush_icache((void *)h->target, 16);
            mprotect((void *)ps, span, PROT_READ | PROT_EXEC);
        }
        if (h->trampoline)
            munmap((void *)h->trampoline, h->tramp_size);
        h->active = 0;
        h->trampoline = 0;
    }
    g_n_fhooks = 0;
    LOGI("fhook: все хуки сняты");
#endif
}

const br_fhook_t *br_fhook_list(int *count)
{
    if (count) *count = g_n_fhooks;
    return g_fhooks;
}
