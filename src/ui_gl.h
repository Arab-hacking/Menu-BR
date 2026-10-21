/*
 * ui_gl.h — ImGui + OpenGL ES 3 (рендерер написан поверх g_gl, без NDK)
 */
#ifndef BR_UI_GL_H
#define BR_UI_GL_H

#include <stdbool.h>

/* Инициализация/завершение (вызывать с GL-контекстом). */
bool br_uigl_init(int w, int h);
void br_uigl_shutdown(void);

void br_uigl_new_frame(float opacity);
void br_uigl_render(int w, int h);

#endif
