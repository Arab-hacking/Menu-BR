/*
 * elf.cpp — разбор ELF по памяти
 */
#include "br_common.h"
#include <elf.h>

static bool rd_br(const br_elf_t *e, uintptr_t va, void *out, size_t n)
{
    return br_safe_read(e->base + va, out, (int)n);
}

/* имя секции по sh_name */
static bool sec_name(const br_elf_t *e, const Elf64_Shdr *shs, const char *shstr,
                     const Elf64_Shdr *sh, char *out, int outlen)
{
    if (!sh->sh_name || sh->sh_name >= 0x100000) return false;
    if (shstr[sh->sh_name] == 0) return false;
    int n = 0;
    for (int i = 0; i < outlen - 1; i++) {
        out[i] = shstr[sh->sh_name + i];
        n++;
        if (out[i] == 0) break;
    }
    out[n] = 0;
    return n > 0;
}

bool br_elf_load(br_elf_t *e, uintptr_t base)
{
    memset(e, 0, sizeof(*e));
    e->base = base;

    Elf64_Ehdr eh;
    if (!br_safe_read(base, &eh, sizeof(eh)) || memcmp(eh.e_ident, ELFMAG, 4) != 0)
        return false;
    if (eh.e_shnum == 0 || eh.e_shnum > 128 || eh.e_shoff == 0) return false;

    /* читаем таблицу секционных заголовков (небольшая) */
    static Elf64_Shdr shs[128];
    if (!br_safe_read(base + eh.e_shoff, shs, (size_t)eh.e_shnum * sizeof(Elf64_Shdr)))
        return false;

    /* shstrtab */
    static char shstr[8192];
    int shstr_len = 0;
    if (eh.e_shstrndx >= eh.e_shnum) return false;
    shstr_len = (int)shs[eh.e_shstrndx].sh_size;
    if (shstr_len <= 0 || shstr_len >= (int)sizeof(shstr)) return false;
    if (!br_safe_read(base + shs[eh.e_shstrndx].sh_offset, shstr, shstr_len)) return false;
    shstr[shstr_len] = 0;

    char nm[64];
    for (int i = 0; i < eh.e_shnum; i++) {
        char name[64];
        if (!sec_name(e, shs, shstr, &shs[i], name, sizeof(name))) continue;
        if (strcmp(name, ".plt") == 0) {
            e->plt_start = shs[i].sh_addr;
            e->plt_stub_count = (int)(shs[i].sh_size / 0x20);
        } else if (strcmp(name, ".rela.plt") == 0) {
            e->rela_plt_off = shs[i].sh_addr;
            e->rela_plt_count = (int)(shs[i].sh_size / 24);
        } else if (strcmp(name, ".dynsym") == 0) {
            e->dynsym_off = shs[i].sh_addr;
            e->dynsym_count = (int)(shs[i].sh_size / sizeof(Elf64_Sym));
        } else if (strcmp(name, ".dynstr") == 0) {
            e->dynstr_off = shs[i].sh_addr;
        } else if (strcmp(name, ".note.gnu.build-id") == 0) {
            /* note: namesz descsz type name desc — SHA1: namesz=4, descsz=20 */
            uint32_t hdr[3];
            if (rd_br(e, shs[i].sh_addr, hdr, 12) && hdr[0] == 4 && hdr[1] == 20 && hdr[2] == 3) {
                if (rd_br(e, shs[i].sh_addr + 16, e->build_id, 20))
                    e->has_build_id = true;
            }
        }
    }

    if (e->dynsym_count <= 0 || e->dynsym_count > BR_ELF_MAX_SYMS) {
        e->valid = true; /* может быть и без dynsym — но хуки невозможны */
        return true;
    }

    /* кэш динамических символов (только FUNC) */
    for (int i = 0; i < e->dynsym_count && e->n_syms < (int)(int)(sizeof(e->syms) / sizeof(e->syms[0])); i++) {
        Elf64_Sym s;
        if (!rd_br(e, e->dynsym_off + (uintptr_t)i * sizeof(Elf64_Sym), &s, sizeof(s)))
            continue;
        if (ELF64_ST_TYPE(s.st_info) != STT_FUNC) continue;
        if (!s.st_name) continue;
        char buf[96];
        if (!br_elf_read_cstr(e, e->dynstr_off + s.st_name, buf, sizeof(buf))) continue;
        if (strlen(buf) >= sizeof(e->syms[0].name)) continue;
        memcpy(e->syms[e->n_syms].name, buf, strlen(buf) + 1);
        e->syms[e->n_syms].idx = i;
        e->n_syms++;
    }

    e->valid = true;
    return true;
}

bool br_elf_symname(const br_elf_t *e, int idx, char *out, int maxlen)
{
    if (idx < 0 || idx >= e->dynsym_count) return false;
    Elf64_Sym s;
    if (!rd_br(e, e->dynsym_off + (uintptr_t)idx * sizeof(Elf64_Sym), &s, sizeof(s)))
        return false;
    return br_elf_read_cstr(e, e->dynstr_off + s.st_name, out, maxlen);
}

int br_elf_find_sym(const br_elf_t *e, const char *name)
{
    for (int i = 0; i < e->n_syms; i++)
        if (strcmp(e->syms[i].name, name) == 0)
            return e->syms[i].idx;
    return -1;
}

bool br_elf_plt_for_symbol(const br_elf_t *e, const char *name,
                           uintptr_t *stub_va, uintptr_t *got_va)
{
    int sym = br_elf_find_sym(e, name);
    if (sym < 0 || e->rela_plt_count <= 0) return false;

    for (int j = 0; j < e->rela_plt_count; j++) {
        Elf64_Rela r;
        if (!rd_br(e, e->rela_plt_off + (uintptr_t)j * sizeof(Elf64_Rela), &r, sizeof(r)))
            continue;
        uint32_t type = (uint32_t)(r.r_info & 0xFFFFFFFF);
        int r_sym = (int)(r.r_info >> 32);
        if (type == 0x402 /* R_AARCH64_JUMP_SLOT */ && r_sym == sym) {
            *got_va = r.r_offset;
            *stub_va = e->plt_start + 0x20 * ((uintptr_t)j + 1);
            return true;
        }
    }
    return false;
}

bool br_elf_read_cstr(const br_elf_t *e, uintptr_t va, char *out, int maxlen)
{
    int i = 0;
    char c = 0;
    while (i < maxlen - 1) {
        if (!br_safe_read(e->base + va + (uintptr_t)i, &c, 1)) return false;
        if (c == 0) break;
        out[i++] = c;
    }
    out[i] = 0;
    return true;
}

int br_elf_scan(const br_elf_t *e, uintptr_t off, uintptr_t len,
                const uint8_t *pat, const uint8_t *mask, int patlen,
                uintptr_t *first)
{
    if (patlen <= 0 || len <= (uintptr_t)patlen) return 0;
    if (!mask) {
        static uint8_t full[64];
        if (patlen > 64) return 0;
        memset(full, 0xFF, (size_t)patlen);
        mask = full;
    }

    /* pos ниже — абс. VA-смещение модуля (стартует с off),
       поэтому базовый адрес берём без off */
    uintptr_t base_va = e->base;
    int count = 0;
    /* читаем блоками по 64KB, чтобы не делать миллион safe_read */
    const int BLK = 65536;
    static uint8_t buf[BLK];
    uintptr_t pos = off;
    while (pos + (uintptr_t)patlen <= off + len) {
        uintptr_t remain = off + len - pos;
        int chunk = (int)(remain < (uintptr_t)BLK ? remain : (uintptr_t)BLK);
        if (chunk < patlen) break;
        if (!br_safe_read(base_va + pos, buf, chunk)) break;
        /* старты, при которых паттерн целиком внутри блока */
        int lim = chunk - patlen + 1;
        for (int i = 0; i < lim; i++) {
            bool ok = true;
            for (int k = 0; k < patlen; k++) {
                if (mask[k] && buf[i + k] != pat[k]) { ok = false; break; }
            }
            if (ok) {
                count++;
                if (first && *first == 0) *first = pos + (uintptr_t)i;
            }
        }
        /* следующий блок со сдвигом, перекрывающим хвост (patlen-1 байт) */
        if ((uintptr_t)chunk <= (uintptr_t)patlen - 1) break;
        pos += (uintptr_t)chunk - (uintptr_t)(patlen - 1);
    }
    return count;
}
