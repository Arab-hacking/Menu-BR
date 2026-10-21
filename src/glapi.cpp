/*
 * glapi.cpp — подгрузка GL/EGL/Android в рантайме
 */
#include "br_common.h"
#include "glapi.h"
#include <dlfcn.h>

br_glapi_t g_gl;

static void *g_libgles = 0, *g_liblegl = 0, *g_libandroid = 0;

#define GLSYM(name) do { \
    g_gl.name = (decltype(g_gl.name))dlsym(g_libgles, #name); \
    if (!g_gl.name) { LOGW("GL: нет " #name); } \
} while (0)

bool br_glapi_init(void)
{
    g_libgles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_libgles) g_libgles = dlopen("libGLESv3.so", RTLD_NOW);
    if (!g_libgles) {
        g_libgles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_NOLOAD);
        if (!g_libgles) g_libgles = dlopen("libGLESv2.so", RTLD_NOW);
    }
    if (!g_libgles) { LOGE("libGLESv3/v2 не найдены"); return false; }

    GLSYM(glCreateShader);
    GLSYM(glDeleteShader);
    GLSYM(glShaderSource);
    GLSYM(glCompileShader);
    GLSYM(glGetShaderiv);
    GLSYM(glCreateProgram);
    GLSYM(glDeleteProgram);
    GLSYM(glAttachShader);
    GLSYM(glLinkProgram);
    GLSYM(glGetProgramiv);
    GLSYM(glGetUniformLocation);
    GLSYM(glUseProgram);
    GLSYM(glUniform1i);
    GLSYM(glUniform4f);
    GLSYM(glUniform2f);
    GLSYM(glGenBuffers);
    GLSYM(glDeleteBuffers);
    GLSYM(glBindBuffer);
    GLSYM(glBufferData);
    GLSYM(glBufferSubData);
    GLSYM(glGenVertexArrays);
    GLSYM(glDeleteVertexArrays);
    GLSYM(glBindVertexArray);
    GLSYM(glGenTextures);
    GLSYM(glDeleteTextures);
    GLSYM(glBindTexture);
    GLSYM(glTexImage2D);
    GLSYM(glTexParameteri);
    GLSYM(glEnableVertexAttribArray);
    GLSYM(glVertexAttribPointer);
    GLSYM(glDrawElements);
    GLSYM(glDrawArrays);
    GLSYM(glClear);
    GLSYM(glClearDepthf);
    GLSYM(glDisable);
    GLSYM(glEnable);
    GLSYM(glBlendFunc);
    GLSYM(glDepthMask);
    GLSYM(glViewport);
    GLSYM(glScissor);
    GLSYM(glPixelStorei);
    GLSYM(glGetError);
    GLSYM(glGetIntegerv);

    g_gl.gl_ok = g_gl.glDrawElements && g_gl.glCreateShader && g_gl.glGenBuffers;
    if (!g_gl.gl_ok) { LOGE("GL: критичные функции отсутствуют"); return false; }

    g_liblegl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_liblegl) g_liblegl = dlopen("libEGL.so", RTLD_NOW);
    if (g_liblegl) {
        g_gl.eglGetCurrentSurface = (decltype(g_gl.eglGetCurrentSurface))dlsym(g_liblegl, "eglGetCurrentSurface");
        g_gl.eglGetCurrentContext = (decltype(g_gl.eglGetCurrentContext))dlsym(g_liblegl, "eglGetCurrentContext");
        g_gl.egl_ok = g_gl.eglGetCurrentContext != 0;
    }

    g_libandroid = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_libandroid) g_libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (g_libandroid) {
        g_gl.anGetInputQueue = (decltype(g_gl.anGetInputQueue))dlsym(g_libandroid, "ANativeWindow_getInputQueue");
        g_gl.anAcquire = (decltype(g_gl.anAcquire))dlsym(g_libandroid, "AInputQueue_acquireLooseEvent");
        g_gl.anFinish = (decltype(g_gl.anFinish))dlsym(g_libandroid, "AInputQueue_finishEvent");
        g_gl.anType = (decltype(g_gl.anType))dlsym(g_libandroid, "AInputEvent_getType");
        g_gl.anGetId = (decltype(g_gl.anGetId))dlsym(g_libandroid, "AInputEvent_getId");
        g_gl.anAction = (decltype(g_gl.anAction))dlsym(g_libandroid, "AInputEvent_getAction");
        g_gl.anSource = (decltype(g_gl.anSource))dlsym(g_libandroid, "AInputEvent_getSource");
        g_gl.anDev = (decltype(g_gl.anDev))dlsym(g_libandroid, "AInputEvent_getDeviceId");
        g_gl.anMCount = (decltype(g_gl.anMCount))dlsym(g_libandroid, "AMotionEvent_getPointerCount");
        g_gl.anMX = (decltype(g_gl.anMX))dlsym(g_libandroid, "AMotionEvent_getX");
        g_gl.anMY = (decltype(g_gl.anMY))dlsym(g_libandroid, "AMotionEvent_getY");
        g_gl.anMPress = (decltype(g_gl.anMPress))dlsym(g_libandroid, "AMotionEvent_getPressure");
        g_gl.anKCode = (decltype(g_gl.anKCode))dlsym(g_libandroid, "AKeyEvent_getKeyCode");
        g_gl.android_ok = g_gl.anGetInputQueue && g_gl.anAcquire && g_gl.anFinish;
        if (!g_gl.android_ok) LOGW("Android input API недоступен (меню без тача?)");
    }

    /* окно: eglGetCurrentSurface(EGL_WINDOW=3) */
    if (g_gl.egl_ok) {
        void *surf = g_gl.eglGetCurrentSurface(3);
        g_gl.window = (ANativeWindow *)surf;
    }

    LOGI("glapi: gl=%d egl=%d android=%d window=%p",
         g_gl.gl_ok, g_gl.egl_ok, g_gl.android_ok, (void *)g_gl.window);
    return g_gl.gl_ok;
}
