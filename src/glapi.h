/*
 * glapi.h — GL/EGL/Android API через dlopen (без NDK-заголовков)
 * Библиотека должна работать и в сборках без NDK-сида, поэтому все
 * функции подгружаем в рантайме.
 */
#ifndef BR_GLAPIO_H
#define BR_GLAPIO_H

#include <stdint.h>
#include <stdbool.h>

/* --- GL (libGLESv3): точные сигнатуры --- */
typedef void  (*GLfn_i)(int);
typedef unsigned int (*GLfn_ui_v)(void);
typedef unsigned int (*GLfn_ui_i)(int);
typedef void  (*GLfn_ip)(int, unsigned int *);
typedef void  (*GLfn_ui)(unsigned int);
typedef void  (*GLfn_uu)(unsigned int, unsigned int);
typedef void  (*GLfn_uivv)(unsigned int, int, void *, void *);
typedef int   (*GLfn_i_u_i_i)(unsigned int, int, int *);
typedef void  (*GLfn_uiiv2)(unsigned int, int, int, int);
typedef int   (*GLfn_i_ucs)(unsigned int, const char *);
typedef void  (*GLfn_ii)(int, int);
typedef void  (*GLfn_iii)(int, int, int);
typedef void  (*GLfn_iiii)(int, int, int, int);
typedef void  (*GLfn_iiiv)(int, int, int, const void *);
typedef void  (*GLfn_if)(float);
typedef void  (*GLfn_iiv4f)(int, float, float, float, float);
typedef void  (*GLfn_iiv2f)(int, float, float);
typedef void  (*GLfn_iip)(int, int, unsigned int *);
typedef void  (*GLfn_iicp)(int, const unsigned int *);
typedef void  (*GLfn_iui)(int, unsigned int);
typedef void  (*GLfn_bufferdata)(int, long long, const void *, int);  /* glBufferData */
typedef void  (*GLfn_buffersub)(int, long long, long long, const void *); /* glBufferSubData */
typedef void  (*GLfn_iiiiiiii_c)(int, int, int, int, int, int, int, int, const void *);
typedef void  (*GLfn_iiivv)(int, int, int, const void *);
typedef void  (*GLfn_icp)(int, int *);
typedef void  (*GLfn_iivp)(int, int, int *);
typedef int   (*GLfn_i_v)(void);
typedef void  (*GLfn_uii_u)(unsigned int, int, unsigned int);
typedef void  (*GLfn_u_iiiii_cv)(unsigned int, int, int, int, int, const void *);

typedef const char *(*GLfn_gcps)(int);                /* glGetString */
typedef void (*GLfn_iuiii_cf)(unsigned int, int, int, const float *); /* glUniformMatrix4fv */
typedef int   (*GLfn_i_uc)(unsigned int, const char *); /* glGetAttribLocation */
typedef void (*GLfn_iiuc)(unsigned int, int, void *, char *); /* glGet*InfoLog */
typedef void (*GLfn_iboolv)(int, unsigned char *);    /* glGetBooleanv */
typedef int   (*GLfn_i_b)(int);                        /* glIsEnabled */
typedef int   (*GLfn_ui_b)(unsigned int);              /* glIsProgram */

/* --- EGL --- */
typedef void *(*EGLfn_getsurface)(int);
typedef void *(*EGLfn_getdisplay)(void);
typedef int   (*EGLfn_querysurf)(void *, void *, int, int *);

/* --- Android input (libandroid.so) --- */
typedef struct ANativeWindow ANativeWindow;
typedef struct AInputQueue AInputQueue;
typedef struct AInputEvent AInputEvent;

typedef AInputQueue *(*ANfn_getiq)(ANativeWindow *);
typedef bool        (*ANfn_acquire)(AInputQueue *, AInputEvent **, int64_t);
typedef void        (*ANfn_finish)(AInputQueue *, AInputEvent *, bool);
typedef int32_t     (*ANfn_type)(AInputEvent *);
typedef int32_t     (*ANfn_getid)(AInputEvent *);
typedef int32_t     (*ANfn_action)(AInputEvent *);
typedef int32_t     (*ANfn_source)(AInputEvent *);
typedef int32_t     (*ANfn_dev)(AInputEvent *);
typedef int32_t     (*ANfn_mcount)(AInputEvent *);
typedef float       (*ANfn_mx)(AInputEvent *, int32_t);
typedef float       (*ANfn_my)(AInputEvent *, int32_t);
typedef float       (*ANfn_mpress)(AInputEvent *, int32_t);
typedef int32_t     (*ANfn_kcode)(AInputEvent *);

typedef struct {
    /* GL */
    GLfn_ui_i    glfCreateShader;
    GLfn_ui      glfDeleteShader;
    GLfn_uivv    glfShaderSource;
    GLfn_ui      glfCompileShader;
    GLfn_i_u_i_i glfGetShaderiv;
    GLfn_ui_v    glfCreateProgram;
    GLfn_ui      glfDeleteProgram;
    GLfn_uu      glfAttachShader;
    GLfn_ui      glfLinkProgram;
    GLfn_i_u_i_i glfGetProgramiv;
    GLfn_i_ucs   glfGetUniformLocation;
    GLfn_ui      glfUseProgram;
    GLfn_ii      glfUniform1i;
    GLfn_iiv4f   glfUniform4f;
    GLfn_iiv2f   glfUniform2f;
    GLfn_ip      glfGenBuffers;
    GLfn_iicp    glfDeleteBuffers;
    GLfn_iui     glfBindBuffer;
    GLfn_bufferdata glfBufferData;
    GLfn_buffersub glfBufferSubData;
    GLfn_ip      glfGenVertexArrays;
    GLfn_iicp    glfDeleteVertexArrays;
    GLfn_ui      glfBindVertexArray;
    GLfn_ip      glfGenTextures;
    GLfn_iicp    glfDeleteTextures;
    GLfn_iui     glfBindTexture;
    GLfn_iiiiiiii_c glfTexImage2D;
    GLfn_iii     glfTexParameteri;
    GLfn_ui      glfEnableVertexAttribArray;
    GLfn_u_iiiii_cv glfVertexAttribPointer;
    GLfn_iiiv    glfDrawElements;
    GLfn_iii     glfDrawArrays;
    GLfn_i       glfClear;
    GLfn_if      glfClearDepthf;
    GLfn_i       glfDisable;
    GLfn_i       glfEnable;
    GLfn_ii      glfBlendFunc;
    GLfn_i       glfDepthMask;
    GLfn_iiii    glfViewport;
    GLfn_iiii    glfScissor;
    GLfn_ii      glfPixelStorei;
    GLfn_i_v     glfGetError;
    GLfn_icp     glfGetIntegerv;
    GLfn_gcps    glfGetString;
    GLfn_iuiii_cf glfUniformMatrix4fv;
    GLfn_uu      glfDetachShader;
    GLfn_i_uc    glfGetAttribLocation;
    GLfn_i       glfActiveTexture;
    GLfn_iui     glfBindSampler;
    GLfn_i_b     glfIsEnabled;
    GLfn_ui_b    glfIsProgram;
    GLfn_i       glfBlendEquation;
    GLfn_iiii    glfBlendFuncSeparate;
    GLfn_ii      glfBlendEquationSeparate;
    GLfn_iiuc    glfGetShaderInfoLog;
    GLfn_iiuc    glfGetProgramInfoLog;
    GLfn_iboolv  glfGetBooleanv;
    GLfn_iui     glfBindFramebuffer;
    GLfn_iiii    glfColorMask;

    /* EGL */
    EGLfn_getdisplay eglGetCurrentDisplay;
    EGLfn_querysurf  eglQuerySurface;

    /* EGL */
    EGLfn_getsurface eglGetCurrentSurface;
    void *          (*eglGetCurrentContext)(void);

    /* Android */
    ANfn_getiq   anGetInputQueue;
    ANfn_acquire anAcquire;
    ANfn_finish  anFinish;
    ANfn_type    anType;
    ANfn_getid   anGetId;
    ANfn_action  anAction;
    ANfn_source  anSource;
    ANfn_dev     anDev;
    ANfn_mcount  anMCount;
    ANfn_mx      anMX;
    ANfn_my      anMY;
    ANfn_mpress  anMPress;
    ANfn_kcode   anKCode;

    bool gl_ok;
    bool egl_ok;
    bool android_ok;
    ANativeWindow *window;
} br_glapi_t;

extern br_glapi_t g_gl;

bool br_glapi_init(void);

/* Адрес eglSwapBuffers в libEGL (для function-hook; 0 = нет). */
void *br_glapi_eglswap_sym(void);

#endif
