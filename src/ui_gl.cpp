/*
 * ui_gl.cpp — ImGui OpenGL ES 3 backend (собственный, без NDK-заголовков)
 * По образцу examples/opengl3/imgui_impl_opengl3.cpp, но через g_gl.
 */
#include "br_common.h"
#include "ui_gl.h"
#include "glapi.h"
#include <imgui.h>

#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_2D   0x1001
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW  0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT        0x1406
#define GL_TRIANGLES    0x0004
#define GL_BLEND        0x0BE2
#define GL_DEPTH_TEST   0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE0     0x84C0
#define GL_RGBA         0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_NEAREST      0x2600
#define GL_REPEAT       0x2901
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x00004000

static const char *VS_SRC =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform vec4 uScaleTrans;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "void main(){\n"
    "  vUV = aUV;\n"
    "  vColor = aColor;\n"
    "  vec4 p = vec4(aPos, 0.0, 1.0);\n"
    "  p.xy = p.xy * uScaleTrans.xy + uScaleTrans.zw;\n"
    "  gl_Position = p;\n"
    "}\n";

static const char *FS_SRC =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 outColor;\n"
    "void main(){ outColor = vColor * texture(uTex, vUV); }\n";

typedef struct {
    unsigned int vao, vbo, ebo, tex, prog;
    int loc_uScaleTrans, loc_uTex, loc_aPos, loc_aUV, loc_aColor;
    bool ready;
    float w, h;
} uigl_ctx_t;

static uigl_ctx_t g;

bool br_uigl_init(int w, int h)
{
    if (g.ready) return true;
    g.w = w; g.h = h;

    /* --- program --- */
    unsigned int vs = g_gl.glCreateShader(0x8B31 /*VERTEX*/);
    g_gl.glShaderSource(vs, 1, &VS_SRC, 0);
    g_gl.glCompileShader(vs);
    int ok = 0;
    g_gl.glGetShaderiv(vs, 0x8B81 /*COMPILE_STATUS*/, &ok);
    if (!ok) {
        char log[512] = {0};
        /* glGetShaderInfoLog не подгружали; просто жалуемся */
        LOGE("ui_gl: vertex shader не скомпилирован");
        return false;
    }
    unsigned int fs = g_gl.glCreateShader(0x8B30 /*FRAGMENT*/);
    g_gl.glShaderSource(fs, 1, &FS_SRC, 0);
    g_gl.glCompileShader(fs);
    g_gl.glGetShaderiv(fs, 0x8B81, &ok);
    if (!ok) { LOGE("ui_gl: fragment shader не скомпилирован"); return false; }

    g.prog = g_gl.glCreateProgram();
    g_gl.glAttachShader(g.prog, vs);
    g_gl.glAttachShader(g.prog, fs);
    g_gl.glLinkProgram(g.prog);
    g_gl.glGetProgramiv(g.prog, 0x8B82 /*LINK_STATUS*/, &ok);
    if (!ok) { LOGE("ui_gl: program не линкуется"); return false; }
    g_gl.glDeleteShader(vs);
    g_gl.glDeleteShader(fs);

    g.loc_aPos = g_gl.glGetUniformLocation(g.prog, "aPos");
    g.loc_aUV = g_gl.glGetUniformLocation(g.prog, "aUV");
    g.loc_aColor = g_gl.glGetUniformLocation(g.prog, "aColor");
    g.loc_uScaleTrans = g_gl.glGetUniformLocation(g.prog, "uScaleTrans");
    g.loc_uTex = g_gl.glGetUniformLocation(g.prog, "uTex");
    (void)g.loc_aPos; (void)g.loc_aUV; (void)g.loc_aColor; /* layout-qualified */

    /* --- VAO/VBO/EBO --- */
    g_gl.glGenVertexArrays(1, &g.vao);
    g_gl.glGenBuffers(1, &g.vbo);
    g_gl.glGenBuffers(1, &g.ebo);
    g_gl.glBindVertexArray(g.vao);
    g_gl.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    g_gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    g_gl.glEnableVertexAttribArray(0);
    g_gl.glVertexAttribPointer(0, 2, GL_FLOAT, 0, 20, (const void *)0);
    g_gl.glEnableVertexAttribArray(1);
    g_gl.glVertexAttribPointer(1, 2, GL_FLOAT, 0, 20, (const void *)8);
    g_gl.glEnableVertexAttribArray(2);
    g_gl.glVertexAttribPointer(2, 4, GL_FLOAT, 0, 20, (const void *)16);
    g_gl.glBindVertexArray(0);

    /* --- текстура атласа --- */
    g_gl.glGenTextures(1, &g.tex);
    g_gl.glBindTexture(GL_TEXTURE_2D, g.tex);
    g_gl.glTexParameteri(GL_TEXTURE_2D, 0x2801 /*MIN_FILTER*/, GL_NEAREST);
    g_gl.glTexParameteri(GL_TEXTURE_2D, 0x2800 /*MAG_FILTER*/, GL_NEAREST);
    g_gl.glTexParameteri(GL_TEXTURE_2D, 0x2802 /*WRAP_S*/, GL_REPEAT);
    g_gl.glTexParameteri(GL_TEXTURE_2D, 0x2803 /*WRAP_T*/, GL_REPEAT);

    /* атлас шрифта */
    ImFontAtlas *atlas = ImGui::GetIO().Fonts;
    atlas->Flags = 0;
    unsigned char *px = 0;
    int tw = 0, th = 0;
    atlas->GetTexDataAsRGBA32(&px, &tw, &th);
    g_gl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    g_gl.glBindTexture(GL_TEXTURE_2D, g.tex);
    g_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    atlas->TexID = (ImTextureID)g.tex;

    g.ready = true;
    LOGI("ui_gl: инициализирован (%dx%d, tex=%u)", w, h, g.tex);
    return true;
}

void br_uigl_shutdown(void)
{
    if (!g.ready) return;
    g_gl.glDeleteVertexArrays(1, &g.vao);
    g_gl.glDeleteBuffers(1, &g.vbo);
    g_gl.glDeleteBuffers(1, &g.ebo);
    g_gl.glDeleteTextures(1, &g.tex);
    g_gl.glDeleteProgram(g.prog);
    g.ready = false;
}

void br_uigl_new_frame(float opacity)
{
    (void)opacity;
}

void br_uigl_render(int w, int h)
{
    if (!g.ready) return;

    /* реальный размер буфера — из текущего GL viewport (игровой может
     * меняться), если запрос не сработал — используем переданные w/h */
    int vp[4] = {0, 0, w, h};
    if (g_gl.glGetIntegerv) g_gl.glGetIntegerv(0x0BA2 /*GL_VIEWPORT*/, vp);
    int rw = vp[2], rh = vp[3];
    if (rw <= 0 || rh <= 0) { rw = w; rh = h; }

    ImGuiIO *io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)rw, (float)rh);

    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;

    g_gl.glViewport(0, 0, rw, rh);
    g_gl.glDisable(GL_DEPTH_TEST);
    g_gl.glDisable(GL_STENCIL_TEST);
    g_gl.glEnable(GL_BLEND);
    g_gl.glBlendFunc(0x0210 /*SRC_ALPHA*/, 0x0303 /*ONE_MINUS_SRC_ALPHA*/);
    g_gl.glDisable(GL_SCISSOR_TEST);

    g_gl.glBindVertexArray(g.vao);
    g_gl.glUseProgram(g.prog);
    g_gl.glUniform1i(g.loc_uTex, 0);

    for (int n = 0; n < dd->CmdListsCount; n++) {
        ImDrawList *cl = dd->CmdLists[n];
        ImVec2 off = dd->DisplayPos;
        for (int cmd_i = 0; cmd_i < cl->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *cmd = &cl->CmdBuffer[cmd_i];
            if (cmd->UserCallback) continue;
            g_gl.glViewport(cmd->ClipRect.x, cmd->ClipRect.y,
                            cmd->ClipRect.z - cmd->ClipRect.x,
                            cmd->ClipRect.w - cmd->ClipRect.y);
            g_gl.glEnable(GL_SCISSOR_TEST);
            g_gl.glScissor((int)cmd->ClipRect.x, (int)(rh - cmd->ClipRect.w),
                           (int)(cmd->ClipRect.z - cmd->ClipRect.x),
                           (int)(cmd->ClipRect.w - cmd->ClipRect.y));

            g_gl.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
            g_gl.glBufferSubData(GL_ARRAY_BUFFER, 0,
                                 (size_t)cl->VtxBuffer.Size * 20, cl->VtxBuffer.Data);
            g_gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
            g_gl.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                                 (size_t)cl->IdxBuffer.Size * 2, cl->IdxBuffer.Data);
            g_gl.glBindTexture(GL_TEXTURE_2D, (unsigned int)(ImTextureID)cmd->TextureId);

            float sx = 2.0f / (float)rw, sy = -2.0f / (float)rh;
            g_gl.glUniform4f(g.loc_uScaleTrans, sx, sy, -1.0f, 1.0f);

            g_gl.glDrawElements(GL_TRIANGLES, cmd->ElemCount, GL_UNSIGNED_SHORT,
                                (const void *)((size_t)cmd->IdxOffset * 2));
        }
    }
    g_gl.glBindVertexArray(0);
    g_gl.glUseProgram(0);
}
