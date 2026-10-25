/*
 * ui_menu.h — ImGui-меню
 */
#ifndef BR_UI_MENU_H
#define BR_UI_MENU_H

#include <stdbool.h>

void br_ui_init(void);          /* создать ImGui-контекст (без GL) */
void br_frame_render_ui(void);  /* полный UI-кадр (только GL-тред) */

/* Хит-тест: точка внутри открытого окна меню (для swallow кликов, 16.111) */
bool br_ui_menu_hit(float x, float y);

/* Позиция горячей кнопки (для перетаскивания) */
void br_ui_get_btn(float *out_x, float *out_y);

/* Запросить завершение меню (обработка в отдельном треде) */
void br_shutdown_request(void);

#endif
