/*
 * cfg.h — настройки (сохраняются в текстовый файл)
 */
#ifndef BR_CFG_H
#define BR_CFG_H

#include <stdbool.h>

typedef struct {
    int    build;         /* форс сборки: 0=auto, 1=16.106, 2=16.110, 3=16.111, 4=custom */
    float  ui_opacity;    /* 0.3..1 */
    float  menu_scale;    /* 0 = авто, 0.5..2.0 */
    float  radar_range;   /* метры */
    int    esp_blip;      /* размер блипа, px */
    bool   show_players;
    bool   show_vehicles;
    bool   show_labels;
    bool   show_north;
    bool   show_mini_hud; /* мини-HUD (16.111-стиль) */
    int    local_id;      /* -1 = авто */
    bool   turbo_on;
    float  turbo_mult;
    float  btn_x, btn_y;  /* позиция горячей точки (0..1, доля экрана) */
    int    log_level;     /* 0=off 1=err 2=warn 3=info */
} br_cfg_t;

extern br_cfg_t g_cfg;

void br_cfg_init(void);   /* дефолты */
void br_cfg_load(void);
void br_cfg_save(void);
const char *br_cfg_path(void); /* куда сохранялось/будет сохранено */

#endif
