/*
 * input.cpp — ввод через AInputQueue
 *
 * Один палец  = мышь (тап/перетаскивание).
 * Два пальца  = колесо (вертикальный сдвиг центроида) + перетаскивание.
 * Клавиатура  = вперёд, если игровое устройство шлёт key events.
 */
#include "br_common.h"
#include "input.h"
#include "glapi.h"
#include <imgui.h>
#include <pthread.h>

#define INQ_MAX 128

static br_in_ev_t g_q[INQ_MAX];
static int  g_head = 0, g_tail = 0;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static void push(int type, float x, float y, int button, float wheel, int key)
{
    pthread_mutex_lock(&g_mtx);
    int next = (g_tail + 1) % INQ_MAX;
    if (next != g_head) {
        g_q[g_tail].type = type;
        g_q[g_tail].x = x;
        g_q[g_tail].y = y;
        g_q[g_tail].button = button;
        g_q[g_tail].wheel = wheel;
        g_q[g_tail].key = key;
        g_tail = next;
    }
    pthread_mutex_unlock(&g_mtx);
}

int br_input_drain(br_in_ev_t *out, int max)
{
    int n = 0;
    pthread_mutex_lock(&g_mtx);
    while (g_head != g_tail && n < max) {
        out[n++] = g_q[g_head];
        g_head = (g_head + 1) % INQ_MAX;
    }
    pthread_mutex_unlock(&g_mtx);
    return n;
}

/* ImGuiKey mapping (Android keycode -> ImGuiKey, 1.91.9b: именованные константы) */
static int android_key_to_imgui(int32_t kc)
{
    /* буквы и цифры */
    if (kc >= 29 && kc <= 54) return (int)(ImGuiKey_A + (kc - 29));  /* 'A'..'Z' */
    if (kc >= 7 && kc <= 16)  return (int)(ImGuiKey_0 + (kc - 7));   /* '0'..'9' */
    switch (kc) {
    case 1:   return (int)ImGuiKey_Escape;      /* ESCAPE */
    case 62:  return (int)ImGuiKey_Space;       /* SPACE */
    case 66:  return (int)ImGuiKey_Enter;       /* ENTER */
    case 61:  return (int)ImGuiKey_Tab;         /* TAB */
    case 112: return (int)ImGuiKey_Backspace;   /* DEL */
    case 115: return (int)ImGuiKey_Delete;      /* FORWARD_DEL */
    case 21:  return (int)ImGuiKey_LeftArrow;   /* DPAD_LEFT */
    case 22:  return (int)ImGuiKey_RightArrow;  /* DPAD_RIGHT */
    case 19:  return (int)ImGuiKey_UpArrow;     /* DPAD_UP */
    case 20:  return (int)ImGuiKey_DownArrow;   /* DPAD_DOWN */
    case 122: return (int)ImGuiKey_Home;        /* HOME */
    case 123: return (int)ImGuiKey_End;         /* END */
    case 119: return (int)ImGuiKey_PageUp;      /* PAGE_UP */
    case 120: return (int)ImGuiKey_PageDown;    /* PAGE_DOWN */
    case 192: return (int)ImGuiKey_F1;
    case 193: return (int)ImGuiKey_F2;
    case 194: return (int)ImGuiKey_F3;
    case 195: return (int)ImGuiKey_F4;
    case 196: return (int)ImGuiKey_F5;
    case 197: return (int)ImGuiKey_F6;
    case 206: return (int)ImGuiKey_F7;
    case 207: return (int)ImGuiKey_F8;
    case 229: return (int)ImGuiKey_F9;
    case 231: return (int)ImGuiKey_F10;
    case 232: return (int)ImGuiKey_F11;
    case 233: return (int)ImGuiKey_F12;
    case 42:  return (int)ImGuiKey_LeftShift;   /* SHIFT */
    case 56:  return (int)ImGuiKey_RightShift;  /* SHIFT_RIGHT */
    case 57:  return (int)ImGuiKey_LeftCtrl;    /* CTRL_LEFT */
    case 43:  return (int)ImGuiKey_CapsLock;
    case 69:  return (int)ImGuiKey_Comma;
    case 74:  return (int)ImGuiKey_Period;
    case 68:  return (int)ImGuiKey_Minus;
    case 70:  return (int)ImGuiKey_Equal;
    case 71:  return (int)ImGuiKey_Slash;
    case 75:  return (int)ImGuiKey_Semicolon;
    case 76:  return (int)ImGuiKey_LeftBracket;
    case 77:  return (int)ImGuiKey_RightBracket;
    case 73:  return (int)ImGuiKey_Apostrophe;
    case 78:  return (int)ImGuiKey_Backslash;
    case 79:  return (int)ImGuiKey_GraveAccent;
    default:  return -1;
    }
}

static volatile int g_run = 0;
static pthread_t g_tid;

/* состояние тача */
static int      g_active_ptr = -1;   /* первичный pointer index */
static int      g_second_ptr = -1;
static float    g_prev_x = 0, g_prev_y = 0;
static float    g_prev_cent_y = 0;
static int      g_have_prev = 0;

static void handle_motion(AInputEvent *e)
{
    int count = g_gl.anMCount(e);
    int p0 = -1, p1 = -1;
    for (int i = 0; i < count && i < 8; i++) {
        float pr = g_gl.anMPress(e, i);
        if (pr > 0.01f) {
            if (p0 < 0) p0 = i; else if (p1 < 0) p1 = i;
        }
    }

    int act = g_gl.anAction(e);

    if (p0 >= 0) {
        float x = g_gl.anMX(e, p0);
        float y = g_gl.anMY(e, p0);
        if (act == 0 /*DOWN*/) {
            g_active_ptr = p0;
            g_have_prev = 1;
            g_prev_x = x; g_prev_y = y;
            push(IN_MOUSE_DOWN, x, y, 0, 0, 0);
        } else if (act == 1 /*UP*/ || act == 3 /*CANCEL*/) {
            if (p0 == g_active_ptr) {
                g_active_ptr = -1;
                g_have_prev = 0;
                push(IN_MOUSE_UP, x, y, 0, 0, 0);
            }
        } else if (act == 2 /*MOVE*/ && g_have_prev) {
            push(IN_MOUSE_MOVE, x, y, 0, 0, 0);
            g_prev_x = x; g_prev_y = y;
        }
    } else {
        g_active_ptr = -1;
        g_have_prev = 0;
    }

    /* два пальца: колесо по вертикальному сдвигу центроида */
    if (p0 >= 0 && p1 >= 0) {
        float cy = (g_gl.anMY(e, p0) + g_gl.anMY(e, p1)) * 0.5f;
        if (g_prev_cent_y > 0.0f) {
            float dy = g_prev_cent_y - cy;
            if (dy != 0.0f) push(IN_MOUSE_WHEEL, 0, 0, 0, dy / 24.0f, 0);
        }
        g_prev_cent_y = cy;
    } else {
        g_prev_cent_y = 0;
    }
}

static void *input_thread(void *arg)
{
    (void)arg;

    /* ждём окно */
    AInputQueue *q = 0;
    for (;;) {
        if (!g_run) return 0;
        if (!g_gl.window) {
            if (g_gl.egl_ok) {
                void *s = g_gl.eglGetCurrentSurface(3);
                g_gl.window = (ANativeWindow *)s;
            }
        }
        if (g_gl.window && g_gl.android_ok && !q) {
            q = g_gl.anGetInputQueue(g_gl.window);
            if (q) LOGI("input: очередь получена");
        }
        if (q) break;
        usleep(250000);
    }
    if (!q) { LOGE("input: очередь недоступна"); return 0; }

    while (g_run) {
        AInputEvent *e = 0;
        if (g_gl.anAcquire(q, &e, 0)) {
            if (e) {
                int32_t type = g_gl.anType(e);
                int32_t act = g_gl.anAction(e);
                if (type == 0 /* MOTION */) {
                    handle_motion(e);
                } else if (type == 1 /* KEY */) {
                    int32_t kc = g_gl.anKCode(e);
                    int ik = android_key_to_imgui(kc);
                    if (ik > 0) {
                        if (act == 0) push(IN_KEY_DOWN, 0, 0, 0, 0, ik);
                        else if (act == 1) push(IN_KEY_UP, 0, 0, 0, 0, ik);
                    }
                }
                g_gl.anFinish(q, e, true);
            }
        } else {
            usleep(4000); /* ~4мс — опрос, без блокировки */
        }
    }
    return 0;
}

void br_input_start(void)
{
    if (g_run) return;
    g_run = 1;
    if (pthread_create(&g_tid, 0, input_thread, 0) != 0) { g_run = 0; return; }
    pthread_detach(g_tid);
}

void br_input_stop(void)
{
    g_run = 0;
}
