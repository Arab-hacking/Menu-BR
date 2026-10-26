/*
 * ui_gl.h — рендер ImGui поверх GL-контекста игры
 *
 * Бэкенд: официальный Dear ImGui imgui_impl_opengl3 (ES3, "#version 300 es")
 * на наших dlopen'нутых функциях GL (glapi) — тот же путь, что у
 * device-инспектора 16.111 (принцип: собственный изолированный ImGui,
 * save/restore GL-состояния, пересоздание ресурсов при смене EGL-контекста).
 */
#ifndef BR_UI_GL_H
#define BR_UI_GL_H

#include <stdbool.h>

/* Инициализация/завершение (вызывать с GL-контекстом). */
bool br_uigl_init(int w, int h);
void br_uigl_shutdown(void);

/* Подготовка бэкенда к кадру (DO ПЕРЕД ImGui::NewFrame()). */
void br_uigl_new_frame(void);

/* Render + RenderDrawData (save/restore FBO/mask, glBindFramebuffer(0)).
 * Вызывать ПОСЛЕ пользовательских Begin/End и ImGui::NewFrame(). */
void br_uigl_render(int w, int h);

/* Размер поверхности: eglQuerySurface → BSS-глобал → GL viewport.
 * 0 = не определили. Только с текущим GL-контекстом. */
int  br_uigl_surface_size(int *out_w, int *out_h, uintptr_t bss_wh_va);

/* Активный EGL-контекст (для детекта смены и ресоза ресурсов). */
void *br_uigl_current_egl_context(void);

#endif
