/*
 * ui_menu.h — ImGui-меню
 */
#ifndef BR_UI_MENU_H
#define BR_UI_MENU_H

void br_ui_init(void);          /* создать ImGui-контекст (без GL) */
void br_frame_render_ui(void);  /* полный UI-кадр (только GL-тред) */

#endif
