#pragma once
// ============================================================================
// EGL/egl.h — минимальный shim для кросс-компиляции zig cc.
// Покрывает то, что использует internal_imgui_bridge.cpp.
// На устройстве символы резолвятся из libEGL.so (DT_NEEDED).
// ============================================================================

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int        EGLBoolean;
typedef unsigned int EGLint;
typedef void*      EGLDisplay;
typedef void*      EGLSurface;
typedef void*      EGLContext;
typedef void*      EGLConfig;
typedef void*      EGLImage;

#define EGL_TRUE  1
#define EGL_FALSE 0

#define EGL_NO_DISPLAY  ((EGLDisplay)0)
#define EGL_NO_SURFACE  ((EGLSurface)0)
#define EGL_NO_CONTEXT  ((EGLContext)0)
#define EGL_NO_CONFIG   ((EGLConfig)0)

#define EGL_DRAW        0x305E
#define EGL_READ        0x305C
#define EGL_WIDTH       0x3057
#define EGL_HEIGHT      0x3058

#define EGL_NONE        0x3038

EGLBoolean eglQuerySurface(EGLDisplay display, EGLSurface surface,
                           EGLint attribute, EGLint* value);
EGLContext eglGetCurrentContext(void);
EGLDisplay eglGetCurrentDisplay(void);
EGLSurface eglGetCurrentSurface(EGLint reaffle);
void*      eglGetProcAddress(const char* procname);
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface drawSurface,
                          EGLSurface readSurface, EGLContext context);
typedef void* NativeWindowType;
EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                  NativeWindowType native_window, const EGLint* attrib_list);

#ifdef __cplusplus
}
#endif
