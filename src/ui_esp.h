/*
 * ui_esp.h — радар (top-down ESP без проекции камеры)
 */
#ifndef BR_UI_ESP_H
#define BR_UI_ESP_H

#include <imgui.h>

/* Нарисовать радар в draw list: центр (cx,cy), радиус r (px). */
void br_esp_draw_radar(ImDrawList *dl, ImVec2 center, float r);

#endif
