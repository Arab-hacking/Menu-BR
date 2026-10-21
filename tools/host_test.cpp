/*
 * host_test.cpp — хост-тест (x86_64) логики без игры
 *
 * Что проверяется (на файле реального libblackrussia-client.so):
 *   1. br_elf_load: разбор ELF, build-id;
 *   2. build-id = 805be086... = таблица 16.110;
 *   3. PLT: glDrawElements/glClear/glDrawArrays — VA стабов и GOT
 *      (значения, снятые с артефакта);
 *   4. форма 16B-стаба: последняя инструкция `br x17`;
 *   5. .rela.plt: 2358 записей JUMP_SLOT;
 *   6. сигнатуры: GetByNetId @0x1A1C1C8, диспетчер синхронизации @0x19D4664.
 *
 * Запуск: ./br_host_test /путь/libblackrussia-client.so
 * (по умолчанию — reference/libblackrussia-client.so рядом с репо)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <br_elf.h>
#include <off.h>
#include <mem.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; printf("  PASS  " __VA_ARGS__); printf("\n"); } \
    else      { g_fail++; printf("  FAIL  " __VA_ARGS__); printf("\n"); } \
} while (0)

static void hex20(const uint8_t *b, char *out)
{
    for (int i = 0; i < 20; i++) sprintf(out + i * 2, "%02x", b[i]);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "../reference/libblackrussia-client.so";

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return 2; }
    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat"); return 2; }
    uint8_t *map = (uint8_t *)mmap(0, (size_t)st.st_size, PROT_READ,
                                   MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 2; }
    printf("файл: %s (%lld байт)\n", path, (long long)st.st_size);

    br_elf_t e;
    memset(&e, 0, sizeof(e));

    /* кэш /proc/self/maps для br_safe_read */
    br_maps_refresh();

    /* 1. разбор */
    printf("[1] br_elf_load\n");
    CHECK(br_elf_load(&e, (uintptr_t)map), "ELF разобран");
    if (g_fail) { printf("дальше без смысла\n"); return 1; }

    /* 2. build-id */
    printf("[2] build-id\n");
    char a[41] = {0}, b[41] = {0};
    if (e.has_build_id) hex20(e.build_id, a);
    CHECK(e.has_build_id, "build-id найден: %s", e.has_build_id ? a : "?");
    const br_build_table_t *t16110 = &BR_TABLES[BR_BUILD_16110];
    hex20(t16110->build_id, b);
    CHECK(e.has_build_id && memcmp(e.build_id, t16110->build_id, 20) == 0,
          "совпадает с таблицей 16.110 (%s)", b);

    /* 3. PLT-цели (значения сняты с артефакта 16.110.18323) */
    printf("[3] PLT-хуки\n");
    struct { const char *name; uintptr_t stub, got; } tgt[3] = {
        { "glDrawElements", 0x22EA7E0, 0x2415D28 },
        { "glClear",        0x22EA8A0, 0x2415D58 },
        { "glDrawArrays",   0x22EA9A0, 0x2415D98 },
    };
    for (int i = 0; i < 3; i++) {
        uintptr_t stub = 0, got = 0;
        bool ok = br_elf_plt_for_symbol(&e, tgt[i].name, &stub, &got);
        CHECK(ok, "%s: PLT найден", tgt[i].name);
        CHECK(ok && stub == tgt[i].stub,
              "%s: stub VA 0x%llx (ожид. 0x%llx)", tgt[i].name,
              (unsigned long long)stub, (unsigned long long)tgt[i].stub);
        CHECK(ok && got == tgt[i].got,
              "%s: GOT VA 0x%llx (ожид. 0x%llx)", tgt[i].name,
              (unsigned long long)got, (unsigned long long)tgt[i].got);
    }

    /* 4. форма стаба: 16 байт, последняя — br x17 */
    printf("[4] форма PLT-стаба\n");
    const uint8_t *s16 = (const uint8_t *)map + 0x22EA7E0;
    CHECK(s16[12] == 0x20 && s16[13] == 0x02 && s16[14] == 0x1F && s16[15] == 0xD6,
          "glDrawElements-стаб заканчивается `br x17` (%02x %02x %02x %02x)",
          s16[12], s16[13], s16[14], s16[15]);

    /* 5. .rela.plt */
    printf("[5] .rela.plt\n");
    CHECK(e.rela_plt_count == 2358,
          "записей JUMP_SLOT = %d (ожид. 2358)", e.rela_plt_count);

    /* 6. сигнатуры: .text = 0xfd05a0 .. 0x22e7d40 (начало .plt) */
    const uintptr_t text_base = 0xfd05a0;
    const uintptr_t text_end  = e.plt_start;      /* .plt следует за .text */
    printf("[6] сигнатуры (.text 0x%llx..0x%llx)\n",
           (unsigned long long)text_base, (unsigned long long)text_end);
    uintptr_t first = 0;
    int n = br_elf_scan(&e, text_base, text_end - text_base,
                        t16110->sig_getby, t16110->sig_getby_mask,
                        (int)sizeof(t16110->sig_getby), &first);
    CHECK(n >= 1 && first == t16110->get_by_net_id,
          "GetByNetId: %d совпадений, первое 0x%llx (ожид. 0x%llx)",
          n, (unsigned long long)first,
          (unsigned long long)t16110->get_by_net_id);

    first = 0;
    n = br_elf_scan(&e, text_base, text_end - text_base,
                    t16110->sig_disp, t16110->sig_disp_mask,
                    (int)sizeof(t16110->sig_disp), &first);
    CHECK(n >= 1 && first == t16110->dispatcher,
          "диспетчер синхронизации: %d совпадений, первое 0x%llx (ожид. 0x%llx)",
          n, (unsigned long long)first,
          (unsigned long long)t16110->dispatcher);

    munmap(map, (size_t)st.st_size);
    close(fd);

    printf("\nитог: %d PASS / %d FAIL\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
