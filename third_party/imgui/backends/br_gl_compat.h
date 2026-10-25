/*
 * br_gl_compat.h — GL-совместимость для официального imgui_impl_opengl3
 * в режиме IMGUI_IMPL_OPENGL_LOADER_CUSTOM: типы, константы и маппинг
 * glXxx → функции из g_gl (dlopen libGLESv3, см. glapi.h).
 *
 * Значения констант — GL ES 3.0 (совпадают с desktop GL core).
 */
#ifndef BR_GL_COMPAT_H
#define BR_GL_COMPAT_H

#include "glapi.h"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef float GLclampf;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef long long GLintptr;
typedef long long GLsizeiptr;
typedef char GLchar;
typedef void GLvoid;

/* --- константы --- */
#define GL_NONE                     0x0000
#define GL_FALSE                    0x0000
#define GL_TRUE                     0x0001
#define GL_TRIANGLES                0x0004
#define GL_FLOAT                    0x1406
#define GL_UNSIGNED_BYTE            0x1401
#define GL_UNSIGNED_SHORT           0x1403
#define GL_UNSIGNED_INT             0x1405
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_ONE                      0x0001
#define GL_FUNC_ADD                 0x8006
#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03
#define GL_CULL_FACE                0x0B44
#define GL_DEPTH_TEST               0x0B71
#define GL_STENCIL_TEST             0x0B90
#define GL_VIEWPORT                 0x0BA2
#define GL_BLEND                    0x0BE2
#define GL_SCISSOR_TEST             0x0C11
#define GL_SCISSOR_BOX              0x0C10
#define GL_UNPACK_ROW_LENGTH        0x0CF2
#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_FRONT_AND_BACK           0x0408
#define GL_POLYGON_MODE             0x0B40
#define GL_FILL                     0x1B02
#define GL_MAX_TEXTURE_SIZE         0x0D33
#define GL_TEXTURE_2D               0x1001
#define GL_TEXTURE_2D_BINDING_0     0x8069
#define GL_TEXTURE_BINDING_2D       0x8069
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_NEAREST                  0x2600
#define GL_LINEAR                   0x2601
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_REPEAT                   0x2901
#define GL_RGBA                     0x1908
#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_ARRAY_BUFFER_BINDING     0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_PIXEL_UNPACK_BUFFER      0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EC
#define GL_STREAM_DRAW              0x88E0
#define GL_DYNAMIC_DRAW             0x88E8
#define GL_VERTEX_ARRAY_BINDING     0x85B5
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED    0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE       0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE     0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE       0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886E
#define GL_VERTEX_ATTRIB_ARRAY_POINTER    0x8645
#define GL_PRIMITIVE_RESTART        0x8595
#define GL_CLIP_ORIGIN              0x935C
#define GL_UPPER_LEFT               0x0001
#define GL_TEXTURE0                 0x84C0
#define GL_ACTIVE_TEXTURE           0x84E0
#define GL_SAMPLER_BINDING          0x8919
#define GL_BLEND_SRC_RGB            0x80C9
#define GL_BLEND_DST_RGB            0x80C8
#define GL_BLEND_SRC_ALPHA          0x80CB
#define GL_BLEND_DST_ALPHA          0x80CA
#define GL_BLEND_EQUATION_RGB       0x80C9
#define GL_BLEND_EQUATION_ALPHA     0x80CB
#define GL_MAJOR_VERSION            0x821B
#define GL_MINOR_VERSION            0x821C
#define GL_NUM_EXTENSIONS           0x821D
#define GL_CONTEXT_PROFILE_MASK     0x9146
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x00000002
#define GL_CURRENT_PROGRAM          0x8B8D
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_INFO_LOG_LENGTH          0x8B84
#define GL_FRAMEBUFFER              0x8D40
#define GL_FRAMEBUFFER_BINDING      0x8CA6
#define GL_COLOR_WRITEMASK          0x0C23
#define GL_DEPTH_WRITEMASK          0x0B72

/* --- функции (всегда через g_gl — без NEEDED на libGLESv3) --- */
#define glActiveTexture (g_gl.glfActiveTexture)
#define glAttachShader (g_gl.glfAttachShader)
#define glBindBuffer (g_gl.glfBindBuffer)
#define glBindFramebuffer (g_gl.glfBindFramebuffer)
#define glBindSampler (g_gl.glfBindSampler)
#define glBindTexture (g_gl.glfBindTexture)
#define glBindVertexArray (g_gl.glfBindVertexArray)
#define glBlendEquation (g_gl.glfBlendEquation)
#define glBlendEquationSeparate (g_gl.glfBlendEquationSeparate)
#define glBlendFunc (g_gl.glfBlendFunc)
#define glBlendFuncSeparate (g_gl.glfBlendFuncSeparate)
#define glBufferData (g_gl.glfBufferData)
#define glBufferSubData (g_gl.glfBufferSubData)
#define glClear (g_gl.glfClear)
#define glCompileShader (g_gl.glfCompileShader)
#define glCreateProgram (g_gl.glfCreateProgram)
#define glCreateShader (g_gl.glfCreateShader)
#define glColorMask (g_gl.glfColorMask)
#define glDeleteBuffers (g_gl.glfDeleteBuffers)
#define glDeleteProgram (g_gl.glfDeleteProgram)
#define glDeleteShader (g_gl.glfDeleteShader)
#define glDeleteTextures (g_gl.glfDeleteTextures)
#define glDeleteVertexArrays (g_gl.glfDeleteVertexArrays)
#define glDepthMask (g_gl.glfDepthMask)
#define glDetachShader (g_gl.glfDetachShader)
#define glDisable (g_gl.glfDisable)
#define glDisableVertexAttribArray (g_gl.glfDisableVertexAttribArray)
#define glDrawElements (g_gl.glfDrawElements)
#define glDrawArrays (g_gl.glfDrawArrays)
#define glEnable (g_gl.glfEnable)
#define glEnableVertexAttribArray (g_gl.glfEnableVertexAttribArray)
#define glGenBuffers (g_gl.glfGenBuffers)
#define glGenTextures (g_gl.glfGenTextures)
#define glGenVertexArrays (g_gl.glfGenVertexArrays)
#define glGetAttribLocation (g_gl.glfGetAttribLocation)
#define glGetBooleanv (g_gl.glfGetBooleanv)
#define glGetError (g_gl.glfGetError)
#define glGetIntegerv (g_gl.glfGetIntegerv)
#define glGetProgramInfoLog (g_gl.glfGetProgramInfoLog)
#define glGetProgramiv (g_gl.glfGetProgramiv)
#define glGetShaderInfoLog (g_gl.glfGetShaderInfoLog)
#define glGetShaderiv (g_gl.glfGetShaderiv)
#define glGetString (g_gl.glfGetString)
#define glGetUniformLocation (g_gl.glfGetUniformLocation)
#define glIsEnabled (g_gl.glfIsEnabled)
#define glIsProgram (g_gl.glfIsProgram)
#define glLinkProgram (g_gl.glfLinkProgram)
#define glPixelStorei (g_gl.glfPixelStorei)
#define glScissor (g_gl.glfScissor)
#define glShaderSource (g_gl.glfShaderSource)
#define glTexImage2D (g_gl.glfTexImage2D)
#define glTexParameteri (g_gl.glfTexParameteri)
#define glUniform1i (g_gl.glfUniform1i)
#define glUniformMatrix4fv (g_gl.glfUniformMatrix4fv)
#define glUseProgram (g_gl.glfUseProgram)
#define glVertexAttribPointer (g_gl.glfVertexAttribPointer)
#define glViewport (g_gl.glfViewport)
#endif /* BR_GL_COMPAT_H */
