#pragma once
// ============================================================================
// GLES3/gl3.h — минимальный shim для кросс-компиляции zig cc.
// Покрывает все gl*-символы, которые реально используются в
// imgui_impl_opengl3.cpp (1.90.9, ES3-ветка) и internal_imgui_bridge.cpp.
// На устройстве символы резолвятся из libGLESv3.so (DT_NEEDED).
// ============================================================================

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned char  GLubyte;
typedef char           GLchar;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef float          GLfloat;
typedef double         GLdouble;
typedef int64_t        GLint64;
typedef unsigned long long GLbitfield;
typedef uint64_t       GLuint64;
typedef ptrdiff_t      GLintptr;
typedef size_t         GLsizeiptr;
typedef unsigned short GLhalf;
typedef GLint          GLlocation;
typedef void*          GLsync;
typedef GLint          GLUniformLocation;
typedef void           GLvoid;

#define GL_ES 1

/* ---- Константы ES 3.0 core (значения по спецификации) ---- */
#define GL_FALSE                     0
#define GL_TRUE                      1

#define GL_DEPTH_BUFFER_BIT          0x00000100
#define GL_STENCIL_BUFFER_BIT        0x00000400
#define GL_COLOR_BUFFER_BIT          0x00004000
#define GL_FRAMEBUFFER               0x8D40
#define GL_FRAMEBUFFER_BINDING       0x8CA6
#define GL_RENDERBUFFER              0x8D41

#define GL_POINTS                    0x0000
#define GL_LINES                     0x0001
#define GL_LINE_LOOP                 0x0002
#define GL_LINE_STRIP                0x0003
#define GL_TRIANGLES                 0x0004
#define GL_TRIANGLE_STRIP            0x0005
#define GL_TRIANGLE_FAN              0x0006

#define GL_ZERO                      0
#define GL_ONE                       1

#define GL_VENDOR                    0x1F00
#define GL_RENDERER                  0x1F01
#define GL_VERSION                   0x1F02
#define GL_EXTENSIONS                0x1F03

#define GL_CULL_FACE                 0x0B44
#define GL_DEPTH_TEST                0x0B71
#define GL_STENCIL_TEST              0x0B90
#define GL_DITHER                    0x0BD0
#define GL_BLEND                     0x0BE2
#define GL_SCISSOR_TEST              0x0C11

#define GL_FRONT                     0x0404
#define GL_BACK                      0x0405
#define GL_FRONT_AND_BACK            0x0408

#define GL_POLYGON_MODE              0x0B40
#define GL_FILL                      0x1B02
#define GL_LINE                      0x1B01
#define GL_POINT                     0x1B00

#define GL_COLOR_WRITEMASK           0x0C23
#define GL_DEPTH_WRITEMASK           0x0B72

#define GL_BLEND_DST_RGB             0x80C8
#define GL_BLEND_SRC_RGB             0x80C9
#define GL_BLEND_DST_ALPHA           0x80CA
#define GL_BLEND_SRC_ALPHA           0x80CB
#define GL_FUNC_ADD                  0x8006
#define GL_BLEND_EQUATION_RGB        0x8009
#define GL_BLEND_EQUATION_ALPHA      0x883D
#define GL_FUNC_REVERSE_SUBTRACT     0x80E9
#define GL_FUNC_SUBTRACT             0x80E7
#define GL_SRC_COLOR                 0x0300
#define GL_ONE_MINUS_SRC_COLOR       0x0301
#define GL_SRC_COLOR_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_COLOR_ALPHA 0x0303
#define GL_SRC_ALPHA                 0x0302
#define GL_ONE_MINUS_SRC_ALPHA       0x0303
#define GL_DST_ALPHA                 0x0304
#define GL_ONE_MINUS_DST_ALPHA       0x0305

#define GL_STREAM_DRAW               0x88E0
#define GL_STATIC_DRAW               0x88E4
#define GL_DYNAMIC_DRAW              0x88E8

#define GL_ARRAY_BUFFER              0x8892
#define GL_ELEMENT_ARRAY_BUFFER      0x8893
#define GL_PIXEL_PACK_BUFFER         0x88EB
#define GL_PIXEL_UNPACK_BUFFER       0x88EC
#define GL_ARRAY_BUFFER_BINDING      0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_PIXEL_PACK_BUFFER_BINDING   0x88EB
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EC

#define GL_TEXTURE0                  0x84C0
#define GL_TEXTURE1                  0x84C1
#define GL_TEXTURE2                  0x84C2
#define GL_TEXTURE3                  0x84C3
#define GL_TEXTURE_MAX_UNITS         16

#define GL_TEXTURE_2D                0x0DE1
#define GL_TEXTURE_3D                0x806F
#define GL_TEXTURE_BINDING_2D        0x8069
#define GL_TEXTURE_MAG_FILTER        0x2800
#define GL_TEXTURE_MIN_FILTER        0x2801
#define GL_TEXTURE_WRAP_S            0x2802
#define GL_TEXTURE_WRAP_T            0x2803

#define GL_NEAREST                   0x2600
#define GL_LINEAR                    0x2601
#define GL_NEAREST_MIPMAP_NEAREST    0x2700
#define GL_LINEAR_MIPMAP_LINEAR      0x2703
#define GL_CLAMP_TO_EDGE             0x812F
#define GL_REPEAT                    0x2901

#define GL_RGBA                      0x1908
#define GL_RGB                       0x1907
#define GL_RED                       0x1903

#define GL_UNSIGNED_BYTE             0x1401
#define GL_UNSIGNED_SHORT            0x1403
#define GL_UNSIGNED_INT              0x1405
#define GL_FLOAT                     0x1406
#define GL_HALF_FLOAT                0x140B

#define GL_VERTEX_SHADER             0x8B31
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_INFO_LOG_LENGTH           0x8B84
#define GL_CURRENT_PROGRAM           0x8B8D

#define GL_BUFFER_SIZE               0x8764
#define GL_TEXTURE_WIDTH             0x1000
#define GL_TEXTURE_HEIGHT            0x1001

#define GL_VIEWPORT                  0x0BA2
#define GL_SCISSOR_BOX               0x0C10
#define GL_ACTIVE_TEXTURE            0x84E0

#define GL_UNPACK_ROW_LENGTH         0x0CF2
#define GL_UNPACK_ALIGNMENT          0x0CF5

#define GL_UPPER_LEFT                0x0CAF
#define GL_LOWER_LEFT                0x0CAE
#define GL_CLIP_ORIGIN               0x935C
#define GL_CLIP_DEPTH                0x935D

#define GL_PRIMITIVE_RESTART         0x8558

#define GL_MAJOR_VERSION             0x2024
#define GL_MINOR_VERSION             0x2023
#define GL_NUM_EXTENSIONS            0x1F03

#define GL_VERTEX_ATTRIB_ARRAY_ENABLED   0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE      0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE    0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE      0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886E
#define GL_VERTEX_ATTRIB_ARRAY_POINTER   0x8645

#define GL_VERTEX_ARRAY_BINDING      0x85B5
#define GL_VERTEX_ARRAY_BINDING_OES  0x85B5
#define GL_SAMPLER_BINDING           0x8919

#define GL_FRAGMENT_SHADER_BIT       0x00000002
#define GL_VERTEX_SHADER_BIT         0x00000001

/* ---- Функции ES 3.0 core ---- */
GLenum   glGetError(void);
void     glActiveTexture(GLenum texture);
void     glAttachShader(GLuint program, GLuint shader);
void     glBindAttribLocation(GLuint program, GLuint index, const GLchar* name);
void     glBindBuffer(GLenum target, GLuint buffer);
void     glBindFramebuffer(GLenum target, GLuint framebuffer);
void     glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void     glBindSampler(GLuint unit, GLuint sampler);
void     glBindTexture(GLenum target, GLuint texture);
void     glBindVertexArray(GLuint array);
void     glBlendEquation(GLenum mode);
void     glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void     glBlendFunc(GLenum sfactor, GLenum dfactor);
void     glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void     glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
void     glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
void     glClear(GLbitfield mask);
void     glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void     glClearDepthf(GLfloat depth);
void     glClearStencil(GLint s);
void     glClipControl(GLenum origin, GLenum depth);
void     glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void     glCompileShader(GLuint shader);
GLuint   glCreateProgram(void);
GLuint   glCreateShader(GLenum type);
void     glCullFace(GLenum mode);
void     glDeleteBuffers(GLsizei n, const GLuint* buffers);
void     glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
void     glDeleteProgram(GLuint program);
void     glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers);
void     glDeleteSamplers(GLsizei count, const GLuint* samplers);
void     glDeleteShader(GLuint shader);
void     glDeleteTextures(GLsizei n, const GLuint* textures);
void     glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
void     glDepthFunc(GLenum func);
void     glDepthMask(GLboolean flag);
void     glDepthRangef(GLfloat n, GLfloat f);
void     glDetachShader(GLuint program, GLuint shader);
void     glDisable(GLenum cap);
void     glDisableVertexAttribArray(GLuint index);
void     glDrawArrays(GLenum mode, GLint first, GLsizei count);
void     glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices);
void     glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLuint basevertex);
void     glEnable(GLenum cap);
void     glEnableVertexAttribArray(GLuint index);
void     glFinish(void);
void     glFlush(void);
GLuint   glGenBuffers(GLsizei n, GLuint* buffers);
GLuint   glGenFramebuffers(GLsizei n, GLuint* framebuffers);
GLuint   glGenRenderbuffers(GLsizei n, GLuint* renderbuffers);
GLuint   glGenSamplers(GLsizei count, GLuint* samplers);
GLuint   glGenTextures(GLsizei n, GLuint* textures);
GLuint   glGenVertexArrays(GLsizei n, GLuint* arrays);
void     glGenerateMipmap(GLenum target);
GLint    glGetAttribLocation(GLuint program, const GLchar* name);
GLboolean glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                            GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                            GLbitfield mask, GLenum filter);
void     glGetBooleanv(GLenum pname, GLboolean* data);
void     glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params);
void     glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params);
void     glGetIntegerv(GLenum pname, GLint* data);
void     glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void     glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void     glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
void     glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void     glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
const GLubyte* glGetString(GLenum name);
const GLubyte* glGetStringi(GLenum name, GLuint index);
GLint    glGetUniformLocation(GLuint program, const GLchar* name);
void     glGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params);
void     glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params);
void     glGetVertexAttribPointerv(GLuint index, GLenum pname, void* pointer);
void     glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params);
GLboolean glIsEnabled(GLenum cap);
GLboolean glIsFramebuffer(GLuint framebuffer);
GLboolean glIsProgram(GLuint program);
GLboolean glIsRenderbuffer(GLuint renderbuffer);
GLboolean glIsShader(GLuint shader);
GLboolean glIsTexture(GLuint texture);
GLboolean glIsVertexArray(GLuint array);
void     glLinkProgram(GLuint program);
void     glLoadGen(GLenum gen, GLdouble x, GLdouble y, GLdouble w);
void     glPauseTransformFeedback(void);
void     glPixelStorei(GLenum pname, GLint param);
void     glPolygonMode(GLenum face, GLenum mode);
void     glPolygonOffset(GLfloat factor, GLfloat units);
void     glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                      GLenum format, GLenum type, void* data);
void     glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void     glSampleCoverage(GLfloat value, GLboolean invert);
void     glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void     glShaderBinary(GLsizei count, const GLuint* shaders, GLenum binaryFormat,
                        const void* binary, GLsizei length);
void     glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                        const GLint* length);
void     glStencilFunc(GLenum func, GLint ref, GLuint mask);
void     glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void     glStencilMask(GLuint mask);
void     glStencilMaskSeparate(GLenum face, GLuint mask);
void     glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void     glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
void     glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
void     glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                      GLsizei height, GLsizei depth, GLint border, GLenum format,
                      GLenum type, const void* pixels);
void     glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void     glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
void     glTexParameteri(GLenum target, GLenum pname, GLint param);
void     glTexParameteriv(GLenum target, GLenum pname, const GLint* params);
void     glUniform1f(GLUniformLocation location, GLfloat v0);
void     glUniform1i(GLUniformLocation location, GLint v0);
void     glUniform2f(GLUniformLocation location, GLfloat v0, GLfloat v1);
void     glUniform2i(GLUniformLocation location, GLint v0, GLint v1);
void     glUniform3f(GLUniformLocation location, GLfloat v0, GLfloat v1, GLfloat v2);
void     glUniform4f(GLUniformLocation location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void     glUniform4i(GLUniformLocation location, GLint v0, GLint v1, GLint v2, GLint v3);
void     glUniformMatrix2fv(GLUniformLocation location, GLsizei count, GLboolean transpose, const GLfloat* value);
void     glUniformMatrix3fv(GLUniformLocation location, GLsizei count, GLboolean transpose, const GLfloat* value);
void     glUniformMatrix4fv(GLUniformLocation location, GLsizei count, GLboolean transpose, const GLfloat* value);
void     glUniform1fv(GLUniformLocation location, GLsizei count, const GLfloat* value);
void     glUniform2fv(GLUniformLocation location, GLsizei count, const GLfloat* value);
void     glUniform3fv(GLUniformLocation location, GLsizei count, const GLfloat* value);
void     glUniform4fv(GLUniformLocation location, GLsizei count, const GLfloat* value);
void     glUniform1iv(GLUniformLocation location, GLsizei count, const GLint* value);
void     glUseProgram(GLuint program);
void     glValidateProgram(GLuint program);
void     glVertexAttrib1f(GLuint index, GLfloat x);
void     glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void     glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void     glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void     glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                               GLsizei stride, const void* pointer);
void     glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

/* OES vertex array objects (fallback-ветки бэкенда) */
void     glBindVertexArrayOES(GLuint array);
void     glDeleteVertexArraysOES(GLsizei n, const GLuint* arrays);
GLuint   glGenVertexArraysOES(GLsizei n, GLuint* arrays);
GLboolean glIsVertexArrayOES(GLuint array);

#ifdef __cplusplus
}
#endif
