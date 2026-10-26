#include "internal_imgui_bridge.hpp"
#include "imgui_internal.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <link.h>
#include <mutex>
#include <sys/stat.h>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <jni.h>
#include "imgui_impl_opengl3.h"
#define BR_BRIDGE_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "BRInspector16111", fmt, ##__VA_ARGS__)
#define BR_BRIDGE_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "BRInspector16111", fmt, ##__VA_ARGS__)
#define BR_BRIDGE_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "BRInspector16111", fmt, ##__VA_ARGS__)
#else
#define BR_BRIDGE_LOGI(fmt, ...) std::fprintf(stdout, "[BRBridge][INFO] " fmt "\n", ##__VA_ARGS__)
#define BR_BRIDGE_LOGW(fmt, ...) std::fprintf(stdout, "[BRBridge][WARN] " fmt "\n", ##__VA_ARGS__)
#define BR_BRIDGE_LOGE(fmt, ...) std::fprintf(stderr, "[BRBridge][ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

#include "arm64_hook.hpp"
#include "safe_memory.hpp"

// Проверка совпадения нашей копии Dear ImGui 1.90.9
static_assert(IMGUI_VERSION_NUM == br::offsets::imgui_va::kEmbeddedImGuiVersionNum,
              "Dear ImGui version must be 1.90.9 (19090)");
static_assert(sizeof(ImGuiIO)      == br::offsets::imgui_va::kExpectedSizeofImGuiIO,
              "sizeof(ImGuiIO) mismatch");
static_assert(sizeof(ImGuiStyle)   == br::offsets::imgui_va::kExpectedSizeofImGuiStyle,
              "sizeof(ImGuiStyle) mismatch");
static_assert(sizeof(ImGuiContext) == br::offsets::imgui_va::kExpectedSizeofImGuiCtx,
              "sizeof(ImGuiContext) mismatch");

namespace br::ui {

namespace {

struct QueuedTouchEvent {
    int   action = 0; // 0=DOWN, 1=UP, 2=MOVE, 3=CANCEL
    float x      = 0.0f;
    float y      = 0.0f;
};

struct WindowHitRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

InternalImGuiRuntimeTable                 g_rt_table{};
InternalImGuiBridge::FrameRenderCallback  g_render_cb;
std::atomic<bool>                         g_attached{false};

// Собственный изолированный контекст Dear ImGui
#if defined(__ANDROID__)
ImGuiContext*                             g_own_ctx       = nullptr;
EGLContext                                g_last_egl_ctx  = EGL_NO_CONTEXT;
uint64_t                                  g_last_frame_ns = 0;
#endif

// Очередь касаний между UI-потоком (MotionEvent) и GLThread
std::mutex                                g_touch_mutex;
QueuedTouchEvent                          g_touch_queue[64]{};
size_t                                    g_touch_count   = 0;
WindowHitRect                             g_win_rects[12]{};
size_t                                    g_win_rect_cnt  = 0;
bool                                      g_capturing_drag = false;

using FnJniStep = void (*)(void* env, void* thiz);
[[maybe_unused]] FnJniStep g_orig_jnilib_step = nullptr;

#if defined(__ANDROID__)
using FnEglSwapBuffers = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surf);
FnEglSwapBuffers g_orig_egl_swap = nullptr;
#endif

// Точная 10-аргументная AAPCS64-сигнатура Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent
using FnJniMultiTouch = void (*)(void* env, void* thiz,
                                 int32_t action, int32_t count,
                                 int32_t x1, int32_t y1,
                                 int32_t x2, int32_t y2,
                                 int32_t x3, int32_t y3);
FnJniMultiTouch g_orig_multitouch = nullptr;

[[maybe_unused]] uint64_t MonotonicNowNs() noexcept {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

[[maybe_unused]] bool FileExistsReadable(const char* path) noexcept {
    struct stat st {};
    return path && (::stat(path, &st) == 0) && (st.st_size > 1024);
}

bool EnqueueTouchAndCheckCapture(int normalized_action, float x, float y) noexcept {
    std::lock_guard<std::mutex> lock(g_touch_mutex);

    if (g_touch_count < (sizeof(g_touch_queue) / sizeof(g_touch_queue[0]))) {
        g_touch_queue[g_touch_count++] = QueuedTouchEvent{normalized_action, x, y};
    } else {
        // Кольцевой сдвиг при переполнении
        std::memmove(&g_touch_queue[0], &g_touch_queue[1],
                     (g_touch_count - 1) * sizeof(QueuedTouchEvent));
        g_touch_queue[g_touch_count - 1] = QueuedTouchEvent{normalized_action, x, y};
    }

    g_rt_table.last_touch_x = x;
    g_rt_table.last_touch_y = y;
    if (normalized_action == 0) {
        g_rt_table.last_touch_down = true;
    } else if (normalized_action == 1 || normalized_action == 3) {
        g_rt_table.last_touch_down = false;
    }
    ++g_rt_table.touch_events_total;

    bool hit_any_window = false;
    for (size_t i = 0; i < g_win_rect_cnt; ++i) {
        const auto& r = g_win_rects[i];
        if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) {
            hit_any_window = true;
            break;
        }
    }

    if (normalized_action == 0) {
        g_capturing_drag = hit_any_window;
        return hit_any_window;
    }
    if (normalized_action == 2) {
        return g_capturing_drag || hit_any_window;
    }
    // На отпускание пальца (UP / CANCEL) сбрасываем захват и пропускаем событие в игру,
    // чтобы в самой игре никогда не "залипал" джойстик или кнопка
    g_capturing_drag = false;
    return false;
}

#if defined(__ANDROID__)

const char* InitStandaloneFonts(ImGuiIO& io, int surf_w, int surf_h) {
    io.Fonts->Clear();

    const int short_side = std::max(480, std::min(surf_w, surf_h));
    const float font_px  = std::clamp(static_cast<float>(short_side) / 38.0f, 18.0f, 28.0f);

    struct FontCandidate {
        const char* path;
        const char* label;
    };
    static constexpr FontCandidate kSystemFonts[] = {
        {"/system/fonts/Roboto-Medium.ttf",   "Roboto-Medium.ttf (Кириллица)"},
        {"/system/fonts/Roboto-Regular.ttf",  "Roboto-Regular.ttf (Кириллица)"},
        {"/system/fonts/NotoSans-Regular.ttf","NotoSans-Regular.ttf (Кириллица)"},
        {"/system/fonts/DroidSans.ttf",       "DroidSans.ttf (Кириллица)"},
    };

    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    for (const auto& fc : kSystemFonts) {
        if (FileExistsReadable(fc.path)) {
            ImFont* f = io.Fonts->AddFontFromFileTTF(
                fc.path, font_px, &cfg, io.Fonts->GetGlyphRangesCyrillic());
            if (f != nullptr) {
                io.FontDefault = f;
                BR_BRIDGE_LOGI("Загружен собственный шрифт ImGui: %s (%.1f px)", fc.path, font_px);
                return fc.label;
            }
        }
    }

    ImFontConfig def_cfg{};
    def_cfg.SizePixels = std::round(font_px);
    io.FontDefault = io.Fonts->AddFontDefault(&def_cfg);
    BR_BRIDGE_LOGW("Системные TTF не найдены, использован встроенный шрифт ImGui Default");
    return "ProggyClean Default";
}

void QuerySurfaceDimensions(EGLDisplay dpy, EGLSurface surf, int& out_w, int& out_h) {
    out_w = 0;
    out_h = 0;

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
        EGLint ew = 0, eh = 0;
        if (eglQuerySurface(dpy, surf, EGL_WIDTH, &ew) == EGL_TRUE &&
            eglQuerySurface(dpy, surf, EGL_HEIGHT, &eh) == EGL_TRUE &&
            ew > 0 && eh > 0) {
            out_w = static_cast<int>(ew);
            out_h = static_cast<int>(eh);
        }
    }

    if ((out_w <= 0 || out_h <= 0) && g_rt_table.module_base != 0) {
        int32_t wh[2]{};
        if (mem::SafeMemory::ReadBytes(
                g_rt_table.module_base + offsets::jni_bridge::kBss_SurfaceSizeWH,
                wh, sizeof(wh)) &&
            wh[0] > 0 && wh[1] > 0) {
            out_w = wh[0];
            out_h = wh[1];
        }
    }

    if (out_w <= 0 || out_h <= 0) {
        GLint vp[4]{};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] > 0 && vp[3] > 0) {
            out_w = static_cast<int>(vp[2]);
            out_h = static_cast<int>(vp[3]);
        }
    }
}

// ===== TEMP DIAG v2 (временная диагностика — удалить когда починим) =====
static int g_diag_probe_glx = 0; // центр нашего окна (GL-координаты) для пиксель-пробы
static int g_diag_probe_gly = 0;

static void BrDrainGlErrors(const char* tag) {
    int first = 0, n = 0;
    for (int i = 0; i < 16; ++i) {
        const int e = static_cast<int>(glGetError());
        if (e == 0) break;
        if (n == 0) first = e;
        ++n;
    }
    if (n > 0) {
        BR_BRIDGE_LOGI("DIAG: GL-ошибки [%s]: %d шт, первая 0x%03X", tag, n, static_cast<unsigned>(first));
    }
}

static void BrDiagProbePixel(const char* tag, int glx, int gly) {
    uint8_t px[4 * 4 * 4] = {0};
    glReadPixels(glx, gly, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, px);
    BR_BRIDGE_LOGI("DIAG: pixel[%s] @gl(%d,%d) = R%02X G%02X B%02X A%02X (центр 4x4)",
                   tag, glx, gly, px[0], px[1], px[2], px[3]);
}

static void BrDiagGlFirstFrame() {
    const char* gl_ver  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_vnd  = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* gl_rnd  = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    BR_BRIDGE_LOGI("DIAG: GL=%s | Vendor=%s | Renderer=%s",
                   gl_ver ? gl_ver : "?", gl_vnd ? gl_vnd : "?", gl_rnd ? gl_rnd : "?");

    // Контроль: ES3-шейдер (fragment С precision — как у imgui)
    const char* vs = "#version 300 es\nlayout(location=0) in vec4 P; void main(){ gl_Position = P; }";
    const char* fs = "#version 300 es\nprecision mediump float;\nlayout(location=0) out vec4 C; void main(){ C = vec4(1.0); }";
    char logbuf[512] = {};
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vs, nullptr);
    glCompileShader(v);
    GLint ok = 0; glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) glGetShaderInfoLog(v, sizeof(logbuf), nullptr, logbuf);
    BR_BRIDGE_LOGI("DIAG: контроль vertex compile=%d %s", ok, ok ? "" : logbuf);
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fs, nullptr);
    glCompileShader(f);
    GLint fok = 0; glGetShaderiv(f, GL_COMPILE_STATUS, &fok);
    logbuf[0] = 0;
    if (!fok) glGetShaderInfoLog(f, sizeof(logbuf), nullptr, logbuf);
    BR_BRIDGE_LOGI("DIAG: контроль fragment(precision) compile=%d %s", fok, fok ? "" : logbuf);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint lok = 0; glGetProgramiv(p, GL_LINK_STATUS, &lok);
    logbuf[0] = 0;
    if (!lok) glGetProgramInfoLog(p, sizeof(logbuf), nullptr, logbuf);
    BR_BRIDGE_LOGI("DIAG: контроль program link=%d %s", lok, lok ? "" : logbuf);
    glDetachShader(p, v); glDetachShader(p, f);
    glDeleteShader(v); glDeleteShader(f); glDeleteProgram(p);

    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    BR_BRIDGE_LOGI("DIAG: текущий FBO=%d (0 = окно EGL, !=0 = свой FBO игры)", fbo);
}
// ===== /TEMP DIAG v2 =====

void RenderStandaloneImGuiFrame(EGLDisplay explicit_dpy, EGLSurface explicit_surf) {
    if (!g_attached.load(std::memory_order_acquire) || !g_render_cb) {
        return;
    }

    static thread_local bool s_in_render = false;
    if (s_in_render) {
        return;
    }
    s_in_render = true;

    EGLContext cur_egl_ctx = eglGetCurrentContext();
    if (cur_egl_ctx == EGL_NO_CONTEXT) {
        s_in_render = false;
        return;
    }

    EGLDisplay dpy  = (explicit_dpy != EGL_NO_DISPLAY) ? explicit_dpy : eglGetCurrentDisplay();
    EGLSurface surf = (explicit_surf != EGL_NO_SURFACE) ? explicit_surf : eglGetCurrentSurface(EGL_DRAW);

    // Если модуль libblackrussia-client.so загрузился позже нас — подхватим его базу на лету
    if (g_rt_table.module_base == 0) {
        const uintptr_t found_base = InternalImGuiBridge::FindModuleBase(offsets::kTargetModuleName);
        if (found_base != 0) {
            InternalImGuiBridge::ResolveSymbols(found_base, g_rt_table, false);
        }
    }

    int surf_w = 0, surf_h = 0;
    QuerySurfaceDimensions(dpy, surf, surf_w, surf_h);
    if (surf_w <= 64 || surf_h <= 64) {
        s_in_render = false;
        return;
    }

    g_rt_table.surface_width  = surf_w;
    g_rt_table.surface_height = surf_h;

    ImGuiContext* prev_imgui_ctx = ImGui::GetCurrentContext();

    if (g_own_ctx == nullptr) {
        g_own_ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_own_ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigInputTrickleEventQueue = true;

        g_rt_table.loaded_font_label = InitStandaloneFonts(io, surf_w, surf_h);
        g_rt_table.standalone_ctx    = g_own_ctx;

        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_last_egl_ctx = cur_egl_ctx;
        BR_BRIDGE_LOGI("Собственный контекст Dear ImGui 1.90.9 инициализирован! Контекст=%p, Экран=%dx%d",
                       static_cast<void*>(g_own_ctx), surf_w, surf_h);
        BrDiagGlFirstFrame(); // TEMP DIAG
    } else {
        ImGui::SetCurrentContext(g_own_ctx);
        if (cur_egl_ctx != g_last_egl_ctx) {
            BR_BRIDGE_LOGI("Обнаружена смена EGLContext (%p -> %p), пересоздаём GLES3-ресурсы ImGui",
                           static_cast<void*>(g_last_egl_ctx), static_cast<void*>(cur_egl_ctx));
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplOpenGL3_Init("#version 300 es");
            g_last_egl_ctx = cur_egl_ctx;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize             = ImVec2(static_cast<float>(surf_w), static_cast<float>(surf_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    const uint64_t now_ns = MonotonicNowNs();
    if (g_last_frame_ns != 0 && now_ns > g_last_frame_ns) {
        const float dt = static_cast<float>(now_ns - g_last_frame_ns) * 1.0e-9f;
        io.DeltaTime = std::clamp(dt, 1.0f / 240.0f, 0.25f);
    } else {
        io.DeltaTime = 1.0f / 60.0f;
    }
    g_last_frame_ns = now_ns;

    // Переносим накопленные события тачскрина в наш собственный ImGuiIO
    {
        QueuedTouchEvent local_events[64];
        size_t local_cnt = 0;
        {
            std::lock_guard<std::mutex> lock(g_touch_mutex);
            local_cnt = g_touch_count;
            if (local_cnt > 0) {
                std::memcpy(local_events, g_touch_queue, local_cnt * sizeof(QueuedTouchEvent));
                g_touch_count = 0;
            }
        }

        for (size_t i = 0; i < local_cnt; ++i) {
            const auto& ev = local_events[i];
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(ev.x, ev.y);
            if (ev.action == 0) {
                io.AddMouseButtonEvent(0, true);
            } else if (ev.action == 1 || ev.action == 3) {
                io.AddMouseButtonEvent(0, false);
            }
        }
    }

    // Сохраняем состояние FBO и масок записи GL перед отрисовкой нашего оверлея
    GLint prev_fbo = 0;
    GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean prev_depth_mask    = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

    // TEMP DIAG v2: ошибки игры (до нас) + пиксель "до" нашего рендера
    BrDrainGlErrors("игра, до нашего рендера");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    {
        int glx = 600, gly = surf_h - 400;
        if (g_diag_probe_glx > 0) { glx = g_diag_probe_glx; gly = g_diag_probe_gly; }
        BrDiagProbePixel("до", glx, gly);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ++g_rt_table.rendered_frames_total;
    g_render_cb(g_rt_table);

    // Обновляем список прямоугольников видимых окон меню для мгновенного перехвата касаний
    {
        WindowHitRect rects[12]{};
        size_t cnt = 0;
        for (int i = 0; i < g_own_ctx->Windows.Size && cnt < 12; ++i) {
            ImGuiWindow* w = g_own_ctx->Windows[i];
            if (!w || !w->WasActive || w->Hidden) {
                continue;
            }
            if ((w->Flags & ImGuiWindowFlags_NoInputs) == ImGuiWindowFlags_NoInputs) {
                continue;
            }
            rects[cnt++] = WindowHitRect{
                w->Pos.x,
                w->Pos.y,
                w->Pos.x + w->Size.x,
                w->Pos.y + w->Size.y
            };
        }
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        std::memcpy(g_win_rects, rects, cnt * sizeof(WindowHitRect));
        g_win_rect_cnt = cnt;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // TEMP DIAG v3: пиксель "после" в центре ГЛАВНОГО окна + наши GL-ошибки + GL-состояние (1-й кадр)
    {
        int glx = 600, gly = surf_h - 400;
        if (g_own_ctx && g_own_ctx->Windows.Size > 0) {
            for (int i = 0; i < g_own_ctx->Windows.Size; ++i) {
                ImGuiWindow* w = g_own_ctx->Windows[i];
                if (!w || !w->WasActive || w->Hidden) continue;
                const bool is_main = std::strstr(w->Name, "BRInspectorMain") != nullptr;
                if (is_main || (g_diag_probe_glx == 0)) {
                    const int cx = static_cast<int>(w->Pos.x + w->Size.x * 0.5f);
                    const int cy = static_cast<int>(w->Pos.y + w->Size.y * 0.5f);
                    if (cx > 4 && cy > 4 && cx < surf_w - 4 && cy < surf_h - 4) {
                        glx = cx; gly = surf_h - cy;
                        g_diag_probe_glx = cx; g_diag_probe_gly = surf_h - cy;
                        if (is_main) {
                            static bool s_name_logged = false;
                            if (!s_name_logged) {
                                s_name_logged = true;
                                BR_BRIDGE_LOGI("DIAG: главное окно: pos=(%.0f,%.0f) size=(%.0f,%.0f) name='%s'",
                                               w->Pos.x, w->Pos.y, w->Size.x, w->Size.y, w->Name);
                            }
                        }
                        break;
                    }
                }
            }
        }
        BrDiagProbePixel("после", glx, gly);
        BrDrainGlErrors("наши, после RenderDrawData");

        if (g_rt_table.rendered_frames_total == 1) {
            GLint vao = 0, vbo = 0, ebo = 0, tex = 0, prog = 0;
            GLint vbo_sz = 0, ebo_sz = 0, tex_w = 0, tex_h = 0, a0 = 0, link = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
            glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
            if (vbo != 0) glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vbo_sz);
            if (ebo != 0) glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &ebo_sz);
            if (tex != 0) {
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tex_w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tex_h);
            }
            glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a0);
            if (prog != 0) glGetProgramiv(static_cast<GLuint>(prog), GL_LINK_STATUS, &link);
            BR_BRIDGE_LOGI("DIAG: GL-состояние после 1-го кадра: VAO=%d VBO=%d(sz=%d) EBO=%d(sz=%d) TEX=%d(%dx%d) Program=%d(link=%d) attrib0_en=%d",
                           vao, vbo, vbo_sz, ebo, ebo_sz, tex, tex_w, tex_h, prog, link, a0);
        }

        ImDrawData* dd0 = ImGui::GetDrawData();
        static int s_diag_logged = 0;
        if (s_diag_logged < 3 || (g_rt_table.rendered_frames_total % 60) == 0) {
            BR_BRIDGE_LOGI("DIAG: кадр=%llu DrawData: CmdLists=%d Vtx=%d Idx=%d",
                           (unsigned long long)g_rt_table.rendered_frames_total,
                           dd0 ? dd0->CmdListsCount : -1,
                           dd0 ? dd0->TotalVtxCount : -1,
                           dd0 ? dd0->TotalIdxCount : -1);
            if (s_diag_logged < 3) ++s_diag_logged;
        }
    }
    // ===== /TEMP DIAG v3 =====

    // Восстанавливаем исходные буферы кадра и маски игры
    glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
    glDepthMask(prev_depth_mask);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));

    ImGui::SetCurrentContext(prev_imgui_ctx);
    s_in_render = false;
}

void Hooked_JNILib_step(void* env, void* thiz) {
    if (g_orig_jnilib_step) {
        g_orig_jnilib_step(env, thiz);
    }
    RenderStandaloneImGuiFrame(EGL_NO_DISPLAY, EGL_NO_SURFACE);
}

EGLBoolean Hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    RenderStandaloneImGuiFrame(dpy, surf);
    return g_orig_egl_swap ? g_orig_egl_swap(dpy, surf) : EGL_FALSE;
}

#endif // defined(__ANDROID__)

void Hooked_JNILib_multiTouchEvent(void* env, void* thiz,
                                   int32_t action, int32_t count,
                                   int32_t x1, int32_t y1,
                                   int32_t x2, int32_t y2,
                                   int32_t x3, int32_t y3) {
    // Преобразуем Android MotionEvent actionMask:
    // 0 (ACTION_DOWN) / 5 (ACTION_POINTER_DOWN) -> 0 (DOWN)
    // 1 (ACTION_UP)   / 6 (ACTION_POINTER_UP)   -> 1 (UP)
    // 2 (ACTION_MOVE) / 7 (ACTION_HOVER_MOVE)   -> 2 (MOVE)
    // 3 (ACTION_CANCEL)                         -> 3 (CANCEL)
    const int action_masked = action & 0xFF;
    int norm_action = 2;
    if (action_masked == 0 || action_masked == 5) {
        norm_action = 0;
    } else if (action_masked == 1 || action_masked == 6) {
        norm_action = 1;
    } else if (action_masked == 3) {
        norm_action = 3;
    }

    const bool swallow_for_menu = EnqueueTouchAndCheckCapture(
        norm_action, static_cast<float>(x1), static_cast<float>(y1));

    if (swallow_for_menu) {
        // Если пользователь нажимает/тянет окно нашего меню — не передаём нажатие в игру
        return;
    }

    if (g_orig_multitouch) {
        g_orig_multitouch(env, thiz, action, count, x1, y1, x2, y2, x3, y3);
    }
}

struct PhdrSearchCtx {
    const char* needle = nullptr;
    uintptr_t   found  = 0;
};

int PhdrFindCallback(struct dl_phdr_info* info, size_t /*size*/, void* data) {
    auto* ctx = static_cast<PhdrSearchCtx*>(data);
    if (!info || !info->dlpi_name || !ctx || !ctx->needle) {
        return 0;
    }
    if (std::strstr(info->dlpi_name, ctx->needle) != nullptr) {
        ctx->found = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

} // namespace

uintptr_t InternalImGuiBridge::FindModuleBase(const char* module_name) noexcept {
    if (!module_name || !*module_name) {
        return 0;
    }

    PhdrSearchCtx ctx{module_name, 0};
    dl_iterate_phdr(&PhdrFindCallback, &ctx);
    if (ctx.found != 0) {
        return ctx.found;
    }

    FILE* fp = std::fopen("/proc/self/maps", "r");
    if (!fp) {
        return 0;
    }

    char line[1024]{};
    uintptr_t best_base = 0;
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, module_name) == nullptr) {
            continue;
        }
        unsigned long long start = 0;
        unsigned long long end   = 0;
        unsigned long long off   = 0;
        if (std::sscanf(line, "%llx-%llx %*4s %llx", &start, &end, &off) == 3) {
            if (off == 0) {
                best_base = static_cast<uintptr_t>(start);
                break;
            }
            if (best_base == 0 || start < best_base) {
                best_base = static_cast<uintptr_t>(start);
            }
        }
    }
    std::fclose(fp);
    return best_base;
}

bool InternalImGuiBridge::ResolveSymbols(uintptr_t module_base,
                                         InternalImGuiRuntimeTable& out_table,
                                         bool require_exact_build_id) noexcept {
    out_table.module_base       = module_base;
    out_table.standalone_mode   = true;
    out_table.build_id_verified = false;
    std::memset(out_table.observed_build_id, 0, sizeof(out_table.observed_build_id));

    if (module_base == 0) {
        return !require_exact_build_id;
    }

    uint8_t magic[4]{};
    if (!mem::SafeMemory::ReadBytes(module_base, magic, sizeof(magic)) ||
        magic[0] != 0x7F || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        return !require_exact_build_id;
    }

    if (mem::SafeMemory::ReadBytes(module_base + offsets::kGnuBuildIdDescVA,
                                   out_table.observed_build_id,
                                   offsets::kGnuBuildIdLength)) {
        out_table.build_id_verified =
            (std::memcmp(out_table.observed_build_id,
                         offsets::kExpectedGnuBuildId,
                         offsets::kGnuBuildIdLength) == 0);
    }

    if (require_exact_build_id && !out_table.build_id_verified) {
        return false;
    }

    namespace iv = offsets::imgui_va;
    out_table.p_gimgui          = reinterpret_cast<ImGuiContext**>(module_base + iv::kGImGuiPtr);
    out_table.p_alloc_user_data = reinterpret_cast<void**>(module_base + iv::kGImAllocatorUserData);
    out_table.p_font_scale      = reinterpret_cast<float*>(module_base + iv::kWrapperFontScaleFloat);
    out_table.p_loaded_fonts    = reinterpret_cast<ImFont**>(module_base + iv::kWrapperLoadedFontsArray);
    out_table.p_frame_active    = reinterpret_cast<uint8_t*>(module_base + iv::kWrapperFrameActiveBool);

    uintptr_t alloc_ptr = 0;
    uintptr_t free_ptr  = 0;
    if (mem::SafeMemory::Read<uintptr_t>(module_base + iv::kGImAllocatorAllocFuncPtr, alloc_ptr) && alloc_ptr != 0) {
        out_table.alloc_fn = reinterpret_cast<ImGuiMemAllocFunc>(alloc_ptr);
    }
    if (mem::SafeMemory::Read<uintptr_t>(module_base + iv::kGImAllocatorFreeFuncPtr, free_ptr) && free_ptr != 0) {
        out_table.free_fn = reinterpret_cast<ImGuiMemFreeFunc>(free_ptr);
    }

    return true;
}

bool InternalImGuiBridge::FeedTouchEvent(int action, float x, float y) noexcept {
    return EnqueueTouchAndCheckCapture(action, x, y);
}

bool InternalImGuiBridge::AttachStandaloneRenderer(uintptr_t module_base,
                                                   FrameRenderCallback on_render_frame) {
    if (g_attached.load(std::memory_order_acquire)) {
        return true;
    }

    ResolveSymbols(module_base, g_rt_table, /*require_exact_build_id=*/false);
    g_render_cb = std::move(on_render_frame);

#if defined(__ANDROID__)
    void* client_so = dlopen(offsets::kTargetModuleName, RTLD_NOLOAD | RTLD_NOW);

    // 1. Поиск точки кадра: сначала экспортированный JNI-символ JNILib_step через dlsym,
    //    затем фиксированный VA 0x01901D44, и резервный вариант — eglSwapBuffers в libEGL.so
    uintptr_t step_addr = 0;
    const char* render_label = "Не установлен";

    if (client_so) {
        void* sym = dlsym(client_so, "Java_com_blackhub_bronline_game_core_JNILib_step");
        if (sym) {
            step_addr    = reinterpret_cast<uintptr_t>(sym);
            render_label = "JNILib_step (dlsym, собственный GLES3)";
        }
    }
    if (step_addr == 0) {
        void* sym = dlsym(RTLD_DEFAULT, "Java_com_blackhub_bronline_game_core_JNILib_step");
        if (sym) {
            step_addr    = reinterpret_cast<uintptr_t>(sym);
            render_label = "JNILib_step (RTLD_DEFAULT, собственный GLES3)";
        }
    }
    if (step_addr == 0 && module_base != 0 && g_rt_table.build_id_verified) {
        step_addr    = module_base + offsets::jni_bridge::kJni_JNILib_step;
        render_label = "JNILib_step (VA 0x01901D44, собственный GLES3)";
    }

    bool render_hooked = false;
    if (step_addr != 0) {
        render_hooked = br::hook::Arm64Hook::Install(
            reinterpret_cast<void*>(step_addr),
            reinterpret_cast<void*>(&Hooked_JNILib_step),
            reinterpret_cast<void**>(&g_orig_jnilib_step));
        if (render_hooked) {
            g_rt_table.render_hook_name = render_label;
            BR_BRIDGE_LOGI("Установлен хук рендера собственного ImGui: %s @ 0x%lx",
                           render_label, static_cast<unsigned long>(step_addr));
        } else {
            BR_BRIDGE_LOGW("Не удалось поставить хук на JNILib_step @ 0x%lx, пробуем eglSwapBuffers...",
                           static_cast<unsigned long>(step_addr));
        }
    }

    if (!render_hooked) {
        void* egl_sym = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
        if (!egl_sym) {
            egl_sym = reinterpret_cast<void*>(eglGetProcAddress("eglSwapBuffers"));
        }
        if (egl_sym) {
            render_hooked = br::hook::Arm64Hook::Install(
                egl_sym,
                reinterpret_cast<void*>(&Hooked_eglSwapBuffers),
                reinterpret_cast<void**>(&g_orig_egl_swap));
            if (render_hooked) {
                g_rt_table.render_hook_name = "eglSwapBuffers (libEGL.so, собственный GLES3)";
                BR_BRIDGE_LOGI("Установлен хук рендера собственного ImGui: eglSwapBuffers @ %p", egl_sym);
            }
        }
    }

    if (!render_hooked) {
        if (client_so) {
            dlclose(client_so);
        }
        BR_BRIDGE_LOGE("Не удалось установить ни JNILib_step, ни eglSwapBuffers!");
        return false;
    }

    // 2. Поиск точки мультитача: dlsym("Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent") -> VA 0x01902270
    uintptr_t touch_addr = 0;
    const char* touch_label = "Не установлен";
    if (client_so) {
        void* sym = dlsym(client_so, "Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent");
        if (sym) {
            touch_addr  = reinterpret_cast<uintptr_t>(sym);
            touch_label = "JNILib_multiTouchEvent (dlsym)";
        }
    }
    if (touch_addr == 0) {
        void* sym = dlsym(RTLD_DEFAULT, "Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent");
        if (sym) {
            touch_addr  = reinterpret_cast<uintptr_t>(sym);
            touch_label = "JNILib_multiTouchEvent (RTLD_DEFAULT)";
        }
    }
    if (touch_addr == 0 && module_base != 0 && g_rt_table.build_id_verified) {
        touch_addr  = module_base + offsets::jni_bridge::kJni_JNILib_multiTouchEvent;
        touch_label = "JNILib_multiTouchEvent (VA 0x01902270)";
    }

    if (touch_addr != 0) {
        if (br::hook::Arm64Hook::Install(
                reinterpret_cast<void*>(touch_addr),
                reinterpret_cast<void*>(&Hooked_JNILib_multiTouchEvent),
                reinterpret_cast<void**>(&g_orig_multitouch))) {
            g_rt_table.touch_hook_name = touch_label;
            BR_BRIDGE_LOGI("Установлен хук мультитача собственного ImGui: %s @ 0x%lx",
                           touch_label, static_cast<unsigned long>(touch_addr));
        } else {
            BR_BRIDGE_LOGW("Не удалось установить хук мультитача @ 0x%lx",
                           static_cast<unsigned long>(touch_addr));
        }
    }

    if (client_so) {
        dlclose(client_so);
    }
#else
    (void)Hooked_JNILib_multiTouchEvent;
    g_rt_table.render_hook_name = "Host Headless Test";
    g_rt_table.touch_hook_name  = "Host Headless Test";
#endif

    g_attached.store(true, std::memory_order_release);
    return true;
}

void InternalImGuiBridge::Detach() noexcept {
    g_attached.store(false, std::memory_order_release);
    br::hook::Arm64Hook::RemoveAll();
    g_orig_multitouch  = nullptr;
    g_orig_jnilib_step = nullptr;
#if defined(__ANDROID__)
    g_orig_egl_swap    = nullptr;
#endif
}

const InternalImGuiRuntimeTable& InternalImGuiBridge::GetRuntimeTable() noexcept {
    return g_rt_table;
}

bool InternalImGuiBridge::IsAttached() noexcept {
    return g_attached.load(std::memory_order_acquire);
}

std::string InternalImGuiBridge::FormatBuildIdHex(const uint8_t build_id[20]) {
    char buf[41]{};
    for (size_t i = 0; i < 20; ++i) {
        std::snprintf(buf + i * 2, 3, "%02x", build_id[i]);
    }
    return std::string(buf);
}

} // namespace br::ui
