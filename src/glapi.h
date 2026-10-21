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
typedef void  (*GLfn_ilcv)(int, long, long, const void *);
typedef void  (*GLfn_iiiiiiii_c)(int, int, int, int, int, int, int, int, const void *);
typedef void  (*GLfn_iiivv)(int, int, int, const void *);
typedef void  (*GLfn_icp)(int, int *);
typedef void  (*GLfn_iivp)(int, int, int *);
typedef int   (*GLfn_i_v)(void);
typedef void  (*GLfn_uii_u)(unsigned int, int, unsigned int);
typedef void  (*GLfn_u_iiiii_cv)(unsigned int, int, int, int, int, const void *);

/* --- EGL --- */
typedef void *(*EGLfn_getsurface)(int);

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
    GLfn_ui_i    glCreateShader;
    GLfn_ui      glDeleteShader;
    GLfn_uivv    glShaderSource;
    GLfn_ui      glCompileShader;
    GLfn_i_u_i_i glGetShaderiv;
    GLfn_ui_v    glCreateProgram;
    GLfn_ui      glDeleteProgram;
    GLfn_uu      glAttachShader;
    GLfn_ui      glLinkProgram;
    GLfn_i_u_i_i glGetProgramiv;
    GLfn_i_ucs   glGetUniformLocation;
    GLfn_ui      glUseProgram;
    GLfn_ii      glUniform1i;
    GLfn_iiv4f   glUniform4f;
    GLfn_iiv2f   glUniform2f;
    GLfn_ip      glGenBuffers;
    GLfn_iicp    glDeleteBuffers;
    GLfn_iui     glBindBuffer;
    GLfn_ilcv    glBufferData;
    GLfn_ilcv    glBufferSubData;
    GLfn_ip      glGenVertexArrays;
    GLfn_iicp    glDeleteVertexArrays;
    GLfn_ui      glBindVertexArray;
    GLfn_ip      glGenTextures;
    GLfn_iicp    glDeleteTextures;
    GLfn_iui     glBindTexture;
    GLfn_iiiiiiii_c glTexImage2D;
    GLfn_iii     glTexParameteri;
    GLfn_ui      glEnableVertexAttribArray;
    GLfn_u_iiiii_cv glVertexAttribPointer;
    GLfn_iiiv    glDrawElements;
    GLfn_iii     glDrawArrays;
    GLfn_i       glClear;
    GLfn_if      glClearDepthf;
    GLfn_i       glDisable;
    GLfn_i       glEnable;
    GLfn_ii      glBlendFunc;
    GLfn_i       glDepthMask;
    GLfn_iiii    glViewport;
    GLfn_iiii    glScissor;
    GLfn_ii      glPixelStorei;
    GLfn_i_v     glGetError;
    GLfn_icp     glGetIntegerv;

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

#endif
