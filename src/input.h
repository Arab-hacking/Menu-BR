/*
 * input.h — ввод: AInputQueue (libandroid.so) → очередь событий ImGui
 */
#ifndef BR_INPUT_H
#define BR_INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IN_MOUSE_MOVE = 0,
    IN_MOUSE_DOWN,
    IN_MOUSE_UP,
    IN_MOUSE_WHEEL,
    IN_KEY_DOWN,
    IN_KEY_UP,
} br_in_type_t;

typedef struct {
    int      type;
    float    x, y;
    int      button;   /* 0 = left */
    float    wheel;
    int      key;      /* ImGuiKey */
} br_in_ev_t;

void br_input_start(void);
void br_input_stop(void);

/* Забрать все накопленные события (вызывать из GL-треда). */
int  br_input_drain(br_in_ev_t *out, int max);

#endif
