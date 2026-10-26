/*
 * ui_gl.cpp — обёртка официального imgui_impl_opengl3 (ES3) + управление
 * GL-состоянием по принципу device-инспектора 16.111:
 *   - собственный изолированный ImGui-контекст (создаётся в ui_menu),
 *   - save/restore FBO + color/depth writemask вокруг отрисовки,
 *   - принудительный glBindFramebuffer(0) (игра может рисовать в FBO),
 *   - размер поверхности: eglQuerySurface → BSS-глобал → viewport.
 */
#include "br_common.h"
#include "glapi.h"
#include "ui_gl.h"
#include <imgui.h>

/* официальный бэкенд, подключённый как custom loader поверх g_gl */
/* бэкенд в Init'е вызывает sscanf(): с glibc-хедерами имя уходит в
 * __isoc99_sscanf (bionic его не экспортирует) — редирект на sscanf
 * bionic через asm-label (единственный работающий вариант в этой цепочке) */
extern "C" int br_bionic_sscanf(const char *fmt, ...) __asm__("sscanf");
#define sscanf(fmt, ...) br_bionic_sscanf(fmt, ##__VA_ARGS__)

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include "../third_party/imgui/backends/br_gl_compat.h"
#include "../third_party/imgui/backends/imgui_impl_opengl3.h"
#include "../third_party/imgui/backends/imgui_impl_opengl3.cpp"

void *br_uigl_current_egl_context(void)
{
    return g_gl.eglGetCurrentContext ? g_gl.eglGetCurrentContext() : 0;
}

int br_uigl_surface_size(int *out_w, int *out_h, uintptr_t bss_wh_va)
{
    *out_w = 0; *out_h = 0;

    /* 1) eglQuerySurface текущего draw-поверхности */
    if (g_gl.eglQuerySurface && g_gl.eglGetCurrentDisplay) {
        void *dpy = g_gl.eglGetCurrentDisplay();
        void *surf = g_gl.eglGetCurrentSurface ? g_gl.eglGetCurrentSurface(1 /*DRAW*/) : 0;
        if (dpy && surf) {
            int ew = 0, eh = 0;
            if (g_gl.eglQuerySurface(dpy, surf, 0x3057 /*EGL_WIDTH*/, &ew) == 1 &&
                g_gl.eglQuerySurface(dpy, surf, 0x3058 /*EGL_HEIGHT*/, &eh) == 1 &&
                ew > 0 && eh > 0) {
                *out_w = ew; *out_h = eh;
                return 1;
            }
        }
    }

    /* 2) BSS-глобал {w,h} игры (16.111: 0x03C0FAD8) */
    if (bss_wh_va) {
        int wh[2] = {0, 0};
        if (br_safe_read(bss_wh_va, wh, 8) && wh[0] > 0 && wh[1] > 0) {
            *out_w = wh[0]; *out_h = wh[1];
            return 1;
        }
    }

    /* 3) GL viewport */
    if (g_gl.glfGetIntegerv) {
        int vp[4] = {0, 0, 0, 0};
        g_gl.glfGetIntegerv(0x0BA2 /*GL_VIEWPORT*/, vp);
        if (vp[2] > 0 && vp[3] > 0) { *out_w = vp[2]; *out_h = vp[3]; return 1; }
    }
    return 0;
}

bool br_uigl_init(int w, int h)
{
    (void)w; (void)h;
    if (ImGui::GetIO().BackendRendererName) return true; /* уже */
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("ui_gl: ImGui_ImplOpenGL3_Init не удался");
        return false;
    }
    LOGI("ui_gl: официальный ES3-бэкенд ImGui готов");
    return true;
}

void br_uigl_shutdown(void)
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererName)
        ImGui_ImplOpenGL3_Shutdown();
}

void br_uigl_new_frame(void)
{
    if (!ImGui::GetIO().BackendRendererName) return;
    ImGui_ImplOpenGL3_NewFrame();
}

/* Render + RenderDrawData с save/restore GL-состояния (принцип
 * RenderStandaloneImGuiFrame инспектора 16.111). */
void br_uigl_render(int w, int h)
{
    (void)w; (void)h;
    if (!ImGui::GetIO().BackendRendererName) return;

    /* --- save GL-состояния игры (как в RenderStandaloneImGuiFrame) --- */
    int prev_fbo = 0;
    unsigned char prev_cm[4] = {1, 1, 1, 1};
    unsigned char prev_dm = 1;
    if (g_gl.glfGetIntegerv) g_gl.glfGetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &prev_fbo);
    if (g_gl.glfGetBooleanv) {
        g_gl.glfGetBooleanv(0x0C23 /*GL_COLOR_WRITEMASK*/, prev_cm);
        g_gl.glfGetBooleanv(0x0B72 /*GL_DEPTH_WRITEMASK*/, &prev_dm);
    }

    /* игра может держать активным FBO (рендер в текстуру) — рисуем в 0 */
    if (g_gl.glfBindFramebuffer) g_gl.glfBindFramebuffer(0x8D40 /*GL_FRAMEBUFFER*/, 0);
    if (g_gl.glfColorMask) g_gl.glfColorMask(1, 1, 1, 1);
    if (g_gl.glfDepthMask) g_gl.glfDepthMask(1);

    static int first_fbo_log = 0;
    if (prev_fbo != 0 && first_fbo_log < 3) {
        first_fbo_log++;
        LOGW("ui_gl: игра держала FBO=%d — оверлей в default framebuffer", prev_fbo);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    /* --- restore --- */
    if (g_gl.glfColorMask) g_gl.glfColorMask(prev_cm[0], prev_cm[1], prev_cm[2], prev_cm[3]);
    if (g_gl.glfDepthMask) g_gl.glfDepthMask(prev_dm);
    if (g_gl.glfBindFramebuffer) g_gl.glfBindFramebuffer(0x8D40, (unsigned int)prev_fbo);
}
