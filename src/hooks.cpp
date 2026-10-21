/*
 * hooks.cpp — PLT-хуки ARM64
 */
#include "br_common.h"
#include "detect.h"
#include "hooks.h"
#include <sys/mman.h>
#include <unistd.h>

volatile int   g_draws_since_clear = 0;
volatile int   g_frame_ui_on = 0;
static double  g_last_clear_ms = 0;
static double  g_last_ui_ms = 0;

br_plt_hook_t g_hook_glDrawElements;
br_plt_hook_t g_hook_glClear;
br_plt_hook_t g_hook_glDrawArrays;

/* UI-кадр (объявление; реализация в ui_menu.cpp) */
void br_frame_render_ui(void);

/* ---------- установка ---------- */

static uintptr_t alloc_tramp_near(uintptr_t stub)
{
    /* пробуем несколько подсказок вокруг стаба, чтобы `b` (±128МБ) достал */
    uintptr_t hints[8];
    hints[0] = stub & ~0xFFFULL;
    int n = 1;
    for (uintptr_t d = 0x100000; d <= 0x8000000 && n < 8; d <<= 1) {
        hints[n++] = (stub >= d) ? (stub - d) : (stub + d);
        if (n < 8 && stub + d <= 0xFFFFFFFFFFFFULL) hints[n++] = stub + d;
    }

    for (int i = 0; i < n; i++) {
        void *p = mmap((void *)hints[i], 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) continue;
        uintptr_t a = (uintptr_t)p;
        if (a + 16 > stub && stub - (a + 16) <= 0x7FFFFFFFULL && a <= stub + 0x7FFFFFFF) {
            return a;
        }
        /* не достает — всё равно берём (fallback: проверим дальше) */
        if (i == n - 1) return a;
        munmap(p, 4096);
    }
    return 0;
}

int br_hook_install(br_plt_hook_t *h, const char *sym, void *hook_fn)
{
    memset(h, 0, sizeof(*h));
    snprintf(h->sym, sizeof(h->sym), "%s", sym);
    h->hook_fn = hook_fn;

    uintptr_t stub_va = 0, got_va = 0;
    if (!br_elf_plt_for_symbol(&g_det.elf, sym, &stub_va, &got_va)) {
        LOGE("hook: PLT-стаб %s не найден", sym);
        return -1;
    }
    h->stub = g_det.base + stub_va;
    h->got = g_det.base + got_va;

    /* читаем оригинал */
    if (!br_safe_read(h->stub, h->orig, 16)) {
        LOGE("hook: нельзя прочитать стаб %s", sym);
        return -1;
    }
    /* проверяем хвост: br x17 = 0xd61f0220 */
    if (h->orig[3] != 0xd61f0220u) {
        LOGW("hook: %s — хвост стаба не 'br x17' (0x%08x), всё равно пробую",
             sym, h->orig[3]);
    }

    h->tramp = alloc_tramp_near(h->stub);
    if (!h->tramp) { LOGE("hook: нет места под трэмплин (%s)", sym); return -1; }

    /* копия оригинала в трэмплин */
    if (mprotect((void *)(h->tramp & ~0xFFFULL), 4096, PROT_READ | PROT_WRITE) != 0) {
        LOGE("hook: mprotect(tramp) %s", sym); munmap((void *)h->tramp, 4096); return -1;
    }
    memcpy((void *)h->tramp, h->orig, 16);

    /* проверяем досягаемость для `b` (последняя инст. стаба, адрес h->stub+12) */
    uintptr_t from = h->stub + 12;
    long long dist = (long long)h->tramp - (long long)from;
    if (dist < -0x2000000 || dist > 0x7E00000) {
        LOGE("hook: %s — трэмплин %p не в радиусе `b` (от %p, dist=%lld)",
             sym, (void *)h->tramp, (void *)from, dist);
        munmap((void *)h->tramp, 4096);
        return -1;
    }

    /* патчим: mprotect RW, b tramp, обратно RX */
    uintptr_t page = h->stub & ~0xFFFULL;
    if (mprotect((void *)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) != 0
     && mprotect((void *)page, 4096, PROT_READ | PROT_WRITE) != 0) {
        LOGE("hook: mprotect(stub) %s", sym);
        return -1;
    }
    int32_t b_off = (int32_t)(dist >> 2);
    uint32_t b_insn = 0x14000000u | (uint32_t)(b_off & 0x3FFFFFF);
    *(volatile uint32_t *)(h->stub + 12) = b_insn;
    mprotect((void *)page, 4096, PROT_READ | PROT_EXEC);
    mprotect((void *)(h->tramp & ~0xFFFULL), 4096, PROT_READ | PROT_EXEC);

    h->installed = true;
    LOGI("hook: %s стаб=0x%lx trampoline=0x%lx", sym, (unsigned long)h->stub, (unsigned long)h->tramp);
    return 0;
}

void br_hook_remove(br_plt_hook_t *h)
{
    if (!h->installed) return;
    uintptr_t page = h->stub & ~0xFFFULL;
    mprotect((void *)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void *)h->stub, h->orig, 16);
    mprotect((void *)page, 4096, PROT_READ | PROT_EXEC);
    munmap((void *)(h->tramp & ~0xFFFULL), 4096);
    h->installed = false;
    LOGI("hook: %s снят", h->sym);
}

/* ---------- обработчики ---------- */

static void ui_maybe_render(void)
{
    if (g_frame_ui_on) return;
    double now = br_now_ms();
    int due = 0;
    if (g_draws_since_clear >= 80 && now - g_last_ui_ms >= 12.0) due = 1;
    else if (g_draws_since_clear > 0 && now - g_last_ui_ms >= 200.0) due = 1;
    if (!due) return;

    g_frame_ui_on = 1;
    g_last_ui_ms = now;
    br_frame_render_ui();
    g_frame_ui_on = 0;
}

void br_hook_glClear(int mask)
{
    if (g_hook_glClear.installed)
        ((void (*)(int))g_hook_glClear.tramp)(mask);
    g_draws_since_clear = 0;
    g_last_clear_ms = br_now_ms();
}

void br_hook_glDrawArrays(int mode, int first, int count)
{
    if (g_hook_glDrawArrays.installed)
        ((void (*)(int, int, int))g_hook_glDrawArrays.tramp)(mode, first, count);
    g_draws_since_clear++;
    ui_maybe_render();
}

void br_hook_glDrawElements(int mode, int count, int type, const void *indices)
{
    if (g_hook_glDrawElements.installed)
        ((void (*)(int, int, int, const void *))g_hook_glDrawElements.tramp)(mode, count, type, indices);
    g_draws_since_clear++;
    ui_maybe_render();
}
