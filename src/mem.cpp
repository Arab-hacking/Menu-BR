/*
 * mem.cpp — безопасная работа с памятью процесса
 */
#include "br_common.h"
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


#define BR_MAX_RANGES 1024

static br_range_t g_ranges[BR_MAX_RANGES];
static int        g_n_ranges = 0;
static pthread_mutex_t g_mem_mtx = PTHREAD_MUTEX_INITIALIZER;

int br_maps_refresh(void)
{
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return -1;

    static char all[384 * 1024];
    ssize_t total = 0;
    while (total < (ssize_t)sizeof(all) - 1) {
        ssize_t r = read(fd, all + total, sizeof(all) - 1 - total);
        if (r <= 0) break;
        total += r;
    }
    close(fd);
    all[total] = 0;

    br_range_t tmp[BR_MAX_RANGES];
    int n = 0;
    char *p = all;
    while (*p && n < BR_MAX_RANGES) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;

        /* "ADDR-ADDR PERMS ..." — парсим руками (bionic не имеет
         * glibc-имени __isoc99_sscanf) */
        unsigned long long s = 0, e = 0;
        char perms[8] = {0};
        const char *dash = strchr(line, '-');
        if (!dash) continue;
        s = strtoull(line, 0, 16);
        e = strtoull(dash + 1, 0, 16);
        const char *sp = strchr(dash + 1, ' ');
        if (!sp || e <= s) continue;
        for (int k = 0; k < 3 && sp[1 + k]; k++) perms[k] = sp[1 + k];
        {
            br_range_t *r = &tmp[n];
            r->start = (uintptr_t)s;
            r->end   = (uintptr_t)e;
            r->perms = 0;
            if (perms[0] == 'r') r->perms |= 1;
            if (perms[1] == 'w') r->perms |= 2;
            if (perms[2] == 'x') r->perms |= 4;
            r->name[0] = 0;
            const char *nm = line + strlen(line);
            while (nm > line && *(nm - 1) == ' ') nm--;
            if (*nm) {
                const char *sl = strrchr(nm, '/');
                if (sl) nm = sl + 1;
                snprintf(r->name, sizeof(r->name), "%s", nm);
            } else {
                snprintf(r->name, sizeof(r->name), "[anon]");
            }
            n++;
        }
        if (!*p) break;
    }

    pthread_mutex_lock(&g_mem_mtx);
    for (int i = 0; i < n; i++) g_ranges[i] = tmp[i];
    g_n_ranges = n;
    pthread_mutex_unlock(&g_mem_mtx);
    return n;
}

bool br_mem_in(uintptr_t addr, uintptr_t len, int need)
{
    if (len == 0) return false;
    if (addr + len < addr) return false;

    pthread_mutex_lock(&g_mem_mtx);
    int n = g_n_ranges;
    /* бинарный поиск по start */
    int lo = 0, hi = n - 1, idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_ranges[mid].start <= addr) { idx = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    bool ok = false;
    if (idx >= 0 && addr >= g_ranges[idx].start && addr + len <= g_ranges[idx].end) {
        int p = g_ranges[idx].perms;
        if ((need & 1) && !(p & 1)) ok = false;
        else if ((need & 2) && !(p & 2)) ok = false;
        else ok = true;
    }
    pthread_mutex_unlock(&g_mem_mtx);
    return ok;
}

bool br_safe_read(uintptr_t addr, void *out, int len)
{
    if (len <= 0) return false;
    if (!br_mem_in(addr, (uintptr_t)len, 1)) return false;
    memcpy(out, (const void *)addr, (size_t)len);
    return true;
}

bool br_safe_write(uintptr_t addr, const void *in, int len)
{
    if (len <= 0) return false;
    if (!br_mem_in(addr, (uintptr_t)len, 2)) return false;
    memcpy((void *)addr, in, (size_t)len);
    return true;
}

bool br_readf(uintptr_t addr, float *out, int n)
{
    return br_safe_read(addr, out, n * 4);
}

/* Базовый адрес модуля = начало первого LOAD-сегмента с учётом min(p_vaddr). */
uintptr_t br_find_module(const char *soname)
{
    br_maps_refresh();

    pthread_mutex_lock(&g_mem_mtx);
    uintptr_t first_start = 0;
    for (int i = 0; i < g_n_ranges; i++) {
        if (strcmp(g_ranges[i].name, soname) == 0 && (g_ranges[i].perms & 4)) {
            if (first_start == 0 || g_ranges[i].start < first_start)
                first_start = g_ranges[i].start;
        }
    }
    pthread_mutex_unlock(&g_mem_mtx);
    if (first_start == 0) return 0;

    /* ELF-заголовок должен быть в начале первой маппинг-области (обычно).
     * Корректный base = first_start - min(p_vaddr) по PT_LOAD. */
    uintptr_t minpv = 0;
    {
        bool have = false;
        Elf64_Ehdr eh;
        if (br_safe_read(first_start, &eh, sizeof(eh)) && memcmp(eh.e_ident, ELFMAG, 4) == 0
            && eh.e_phoff >= 0x1000 && eh.e_phoff < (1u << 28)
            && eh.e_phnum > 0 && eh.e_phnum < 128) {
            for (int i = 0; i < eh.e_phnum; i++) {
                Elf64_Phdr phd;
                if (!br_safe_read(first_start + eh.e_phoff + (size_t)i * sizeof(Elf64_Phdr), &phd, sizeof(phd)))
                    break;
                if (phd.p_type == PT_LOAD) {
                    if (!have || phd.p_vaddr < minpv) { minpv = phd.p_vaddr; have = true; }
                }
            }
        }
    }
    if (minpv > first_start) return first_start; /* защита от переполнения */
    return first_start - minpv;
}

int br_module_data_spans(uintptr_t base, br_span_t *out, int max)
{
    if (base == 0 || !out || max <= 0) return 0;
    Elf64_Ehdr eh;
    if (!br_safe_read(base, &eh, sizeof(eh)) || memcmp(eh.e_ident, ELFMAG, 4) != 0)
        return 0;
    int cnt = eh.e_phnum;
    if (cnt <= 0 || cnt > 128) return 0;
    int n = 0;
    for (int i = 0; i < cnt && n < max; i++) {
        Elf64_Phdr ph;
        if (!br_safe_read(base + eh.e_phoff + (size_t)i * sizeof(Elf64_Phdr), &ph, sizeof(ph)))
            continue;
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) && ph.p_memsz > 0) {
            out[n].start = base + ph.p_vaddr;
            out[n].end   = base + ph.p_vaddr + ph.p_memsz;
            n++;
        }
    }
    return n;
}
