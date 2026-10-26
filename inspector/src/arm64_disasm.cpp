#include "arm64_disasm.hpp"

#include <cinttypes>
#include <cstdio>

namespace br::disasm {

namespace {

const char* RegX(uint32_t r, bool sp_for_31 = false) noexcept {
    static constexpr const char* kNames[32] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30", "xzr"
    };
    if (r >= 31) {
        return sp_for_31 ? "sp" : "xzr";
    }
    return kNames[r];
}

const char* RegW(uint32_t r, bool wsp_for_31 = false) noexcept {
    static constexpr const char* kNames[32] = {
        "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",
        "w8",  "w9",  "w10", "w11", "w12", "w13", "w14", "w15",
        "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
        "w24", "w25", "w26", "w27", "w28", "w29", "w30", "wzr"
    };
    if (r >= 31) {
        return wsp_for_31 ? "wsp" : "wzr";
    }
    return kNames[r];
}

const char* CondName(uint32_t cond) noexcept {
    static constexpr const char* kCond[16] = {
        "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
    };
    return kCond[cond & 0xF];
}

int64_t SignExtend(uint64_t val, unsigned bits) noexcept {
    const uint64_t shift = 64U - bits;
    return static_cast<int64_t>(val << shift) >> shift;
}

} // namespace

void FormatArm64Instruction(uint32_t insn, uintptr_t pc, char* out_buf, size_t out_size) noexcept {
    if (!out_buf || out_size == 0) {
        return;
    }

    if (insn == 0xD503201FU) {
        std::snprintf(out_buf, out_size, "nop");
        return;
    }
    if ((insn & 0xFFFFFC1FU) == 0xD65F0000U) {
        const uint32_t rn = (insn >> 5) & 0x1F;
        if (rn == 30) {
            std::snprintf(out_buf, out_size, "ret");
        } else {
            std::snprintf(out_buf, out_size, "ret     %s", RegX(rn));
        }
        return;
    }
    if ((insn & 0xFFFFFC1FU) == 0xD61F0000U || (insn & 0xFFFFFC1FU) == 0xD63F0000U) {
        const bool link = ((insn >> 21) & 1U) != 0;
        const uint32_t rn = (insn >> 5) & 0x1F;
        std::snprintf(out_buf, out_size, "%-7s %s", link ? "blr" : "br", RegX(rn));
        return;
    }

    // B / BL (imm26)
    if ((insn & 0x7C000000U) == 0x14000000U) {
        const bool link = (insn & 0x80000000U) != 0;
        const int64_t off = SignExtend(insn & 0x03FFFFFFU, 26) * 4;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + off);
        std::snprintf(out_buf, out_size, "%-7s 0x%" PRIXPTR, link ? "bl" : "b", target);
        return;
    }

    // B.cond (imm19)
    if ((insn & 0xFF000010U) == 0x54000000U) {
        const uint32_t cond = insn & 0xF;
        const int64_t off = SignExtend((insn >> 5) & 0x7FFFFU, 19) * 4;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + off);
        char mnem[12]{};
        std::snprintf(mnem, sizeof(mnem), "b.%s", CondName(cond));
        std::snprintf(out_buf, out_size, "%-7s 0x%" PRIXPTR, mnem, target);
        return;
    }

    // CBZ / CBNZ
    if ((insn & 0x7E000000U) == 0x34000000U) {
        const bool is64 = (insn & 0x80000000U) != 0;
        const bool nz   = (insn & 0x01000000U) != 0;
        const uint32_t rt = insn & 0x1F;
        const int64_t off = SignExtend((insn >> 5) & 0x7FFFFU, 19) * 4;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + off);
        std::snprintf(out_buf, out_size, "%-7s %s, 0x%" PRIXPTR,
                      nz ? "cbnz" : "cbz",
                      is64 ? RegX(rt) : RegW(rt),
                      target);
        return;
    }

    // TBZ / TBNZ
    if ((insn & 0x7E000000U) == 0x36000000U) {
        const bool b5   = (insn & 0x80000000U) != 0;
        const bool nz   = (insn & 0x01000000U) != 0;
        const uint32_t bit = (b5 ? 32U : 0U) | ((insn >> 19) & 0x1FU);
        const uint32_t rt  = insn & 0x1F;
        const int64_t off  = SignExtend((insn >> 5) & 0x3FFFU, 14) * 4;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + off);
        std::snprintf(out_buf, out_size, "%-7s %s, #%u, 0x%" PRIXPTR,
                      nz ? "tbnz" : "tbz",
                      b5 ? RegX(rt) : RegW(rt),
                      bit, target);
        return;
    }

    // ADR / ADRP
    if ((insn & 0x1F000000U) == 0x10000000U) {
        const bool is_adrp = (insn & 0x80000000U) != 0;
        const uint32_t rd  = insn & 0x1F;
        const uint64_t immlo = (insn >> 29) & 0x3U;
        const uint64_t immhi = (insn >> 5) & 0x7FFFFU;
        const int64_t imm21  = SignExtend((immhi << 2) | immlo, 21);
        uintptr_t target = 0;
        if (is_adrp) {
            target = static_cast<uintptr_t>(static_cast<intptr_t>(pc & ~static_cast<uintptr_t>(0xFFFULL)) +
                                            (imm21 << 12));
        } else {
            target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + imm21);
        }
        std::snprintf(out_buf, out_size, "%-7s %s, 0x%" PRIXPTR,
                      is_adrp ? "adrp" : "adr", RegX(rd), target);
        return;
    }

    // MOVZ / MOVN / MOVK
    if ((insn & 0x1F800000U) == 0x12800000U) {
        const bool is64    = (insn & 0x80000000U) != 0;
        const uint32_t opc = (insn >> 29) & 0x3U;
        const uint32_t hw  = ((insn >> 21) & 0x3U) * 16U;
        const uint32_t imm = (insn >> 5) & 0xFFFFU;
        const uint32_t rd  = insn & 0x1F;
        const char* mnem   = (opc == 0) ? "movn" : (opc == 2) ? "movz" : (opc == 3) ? "movk" : "mov?";
        if (hw == 0) {
            std::snprintf(out_buf, out_size, "%-7s %s, #0x%x",
                          mnem, is64 ? RegX(rd) : RegW(rd), imm);
        } else {
            std::snprintf(out_buf, out_size, "%-7s %s, #0x%x, lsl #%u",
                          mnem, is64 ? RegX(rd) : RegW(rd), imm, hw);
        }
        return;
    }

    // ADD / SUB (immediate)
    if ((insn & 0x1F000000U) == 0x11000000U) {
        const bool is64  = (insn & 0x80000000U) != 0;
        const bool is_sub = (insn & 0x40000000U) != 0;
        const bool set_f  = (insn & 0x20000000U) != 0;
        const uint32_t sh = ((insn >> 22) & 0x1U) ? 12U : 0U;
        const uint32_t imm = ((insn >> 10) & 0xFFFU) << sh;
        const uint32_t rn = (insn >> 5) & 0x1F;
        const uint32_t rd = insn & 0x1F;
        if (set_f && rd == 31) {
            std::snprintf(out_buf, out_size, "%-7s %s, #0x%x",
                          is_sub ? "cmp" : "cmn",
                          is64 ? RegX(rn, true) : RegW(rn, true), imm);
        } else if (!is_sub && !set_f && imm == 0 && (rd == 31 || rn == 31)) {
            std::snprintf(out_buf, out_size, "mov     %s, %s",
                          is64 ? RegX(rd, true) : RegW(rd, true),
                          is64 ? RegX(rn, true) : RegW(rn, true));
        } else {
            std::snprintf(out_buf, out_size, "%-7s %s, %s, #0x%x",
                          is_sub ? (set_f ? "subs" : "sub") : (set_f ? "adds" : "add"),
                          is64 ? RegX(rd, !set_f) : RegW(rd, !set_f),
                          is64 ? RegX(rn, true) : RegW(rn, true),
                          imm);
        }
        return;
    }

    // ORR / MOV register (shifted register, shift == 0, rn == xzr)
    if ((insn & 0x7FE0FFE0U) == 0x2A0003E0U) {
        const bool is64   = (insn & 0x80000000U) != 0;
        const uint32_t rm = (insn >> 16) & 0x1F;
        const uint32_t rd = insn & 0x1F;
        std::snprintf(out_buf, out_size, "mov     %s, %s",
                      is64 ? RegX(rd) : RegW(rd),
                      is64 ? RegX(rm) : RegW(rm));
        return;
    }

    // STP / LDP (64-bit / 32-bit GPR)
    if ((insn & 0x3E000000U) == 0x28000000U && (insn & 0x04000000U) == 0) {
        const uint32_t opc  = (insn >> 30) & 0x3U;
        const bool is64     = (opc == 2);
        const bool is_load  = ((insn >> 22) & 1U) != 0;
        const uint32_t mode = (insn >> 23) & 0x3U; // 1=post, 2=signed off, 3=pre
        const int64_t imm7  = SignExtend((insn >> 15) & 0x7FU, 7) * (is64 ? 8 : 4);
        const uint32_t rt2  = (insn >> 10) & 0x1F;
        const uint32_t rn   = (insn >> 5) & 0x1F;
        const uint32_t rt   = insn & 0x1F;
        const char* r1 = is64 ? RegX(rt) : RegW(rt);
        const char* r2 = is64 ? RegX(rt2) : RegW(rt2);
        const char* rb = RegX(rn, true);
        if (mode == 3) {
            std::snprintf(out_buf, out_size, "%-7s %s, %s, [%s, #%" PRId64 "]!",
                          is_load ? "ldp" : "stp", r1, r2, rb, imm7);
        } else if (mode == 1) {
            std::snprintf(out_buf, out_size, "%-7s %s, %s, [%s], #%" PRId64,
                          is_load ? "ldp" : "stp", r1, r2, rb, imm7);
        } else {
            std::snprintf(out_buf, out_size, "%-7s %s, %s, [%s, #%" PRId64 "]",
                          is_load ? "ldp" : "stp", r1, r2, rb, imm7);
        }
        return;
    }

    // LDR / STR (unsigned immediate, GPR)
    if ((insn & 0x3B000000U) == 0x39000000U && (insn & 0x04000000U) == 0) {
        const uint32_t size = (insn >> 30) & 0x3U;
        const uint32_t opc  = (insn >> 22) & 0x3U;
        const uint32_t imm12 = ((insn >> 10) & 0xFFFU) << size;
        const uint32_t rn   = (insn >> 5) & 0x1F;
        const uint32_t rt   = insn & 0x1F;
        const bool is_load  = (opc & 1U) != 0;
        const bool is64     = (size == 3);
        const char* mnem = is_load
            ? ((size == 0) ? "ldrb" : (size == 1) ? "ldrh" : "ldr")
            : ((size == 0) ? "strb" : (size == 1) ? "strh" : "str");
        std::snprintf(out_buf, out_size, "%-7s %s, [%s, #0x%x]",
                      mnem, is64 ? RegX(rt) : RegW(rt), RegX(rn, true), imm12);
        return;
    }

    std::snprintf(out_buf, out_size, ".word   0x%08X", insn);
}

} // namespace br::disasm
