/*
 * ui_esp.cpp — радар
 *
 * Top-down проекция без матриц камеры (их в данных нет):
 *   экран = центр + (dX, -dY) * (r / range)
 * Y мира = "север".
 */
#include "br_common.h"
#include "ui_esp.h"

void br_esp_draw_radar(ImDrawList *dl, ImVec2 center, float r)
{
    float range = g_cfg.radar_range > 10.0f ? g_cfg.radar_range : 10.0f;
    float scale = r / range;

    ImU32 bg    = IM_COL32(10, 14, 20, 170);
    ImU32 grid  = IM_COL32(60, 90, 120, 160);
    ImU32 north = IM_COL32(140, 180, 255, 220);
    ImU32 pcol  = IM_COL32(255, 80, 80, 235);
    ImU32 vcol  = IM_COL32(255, 200, 60, 235);
    ImU32 lcol  = IM_COL32(90, 255, 130, 255);
    ImU32 txt   = IM_COL32(220, 230, 240, 230);

    dl->PathArcTo(center, r, 0.0f, 6.283185482f, 48);
    dl->PathFillConvex(bg);
    dl->PathArcTo(center, r, 0.0f, 6.283185482f, 48);
    dl->PathStroke(grid, 0, 1.5f);
    dl->PathArcTo(center, r * 0.5f, 0.0f, 6.283185482f, 32);
    dl->PathStroke(grid, 0, 1.0f);
    dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), grid, 1.0f);
    dl->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), grid, 1.0f);

    /* север */
    if (g_cfg.show_north) {
        dl->AddText(ImVec2(center.x - 4, center.y - r + 3), north, "N");
    }

    const br_player_t *lp = 0;
    int lid = g_st.local_id_eff;
    if (lid >= 0 && lid < BR_MAX_PLAYERS && g_st.players[lid].online)
        lp = &g_st.players[lid];
    if (!lp) return; /* локальный не найден — центр неизвестен */

    /* машины (до игроков, чтобы игроки сверху) */
    if (g_cfg.show_vehicles) {
        for (int i = 0; i < g_st.vehicles_count; i++) {
            br_vehicle_t *v = &g_st.vehicles[i];
            if (!v->valid) continue;
            float dx = v->pos[0] - lp->pos[0];
            float dy = v->pos[1] - lp->pos[1];
            if (dx*dx + dy*dy > range*range) continue;
            ImVec2 p(center.x + dx * scale, center.y - dy * scale);
            float s = (float)g_cfg.esp_blip;
            dl->AddRectFilled(ImVec2(p.x - s, p.y - s), ImVec2(p.x + s, p.y + s), vcol);
            if (g_cfg.show_labels && range < 400.0f) {
                char t[48];
                snprintf(t, sizeof(t), "%u %d", v->net_id, (int)(v->speed * 3.6f));
                dl->AddText(ImVec2(p.x + s + 2, p.y - 6), txt, t);
            }
        }
    }

    /* игроки */
    if (g_cfg.show_players) {
        for (int id = 0; id < BR_MAX_PLAYERS; id++) {
            br_player_t *pl = &g_st.players[id];
            if (!pl->online || id == lid) continue;
            float dx = pl->pos[0] - lp->pos[0];
            float dy = pl->pos[1] - lp->pos[1];
            if (dx*dx + dy*dy > range*range) continue;
            ImVec2 p(center.x + dx * scale, center.y - dy * scale);
            float s = (float)g_cfg.esp_blip;
            dl->AddCircleFilled(p, s, pcol);
            if (g_cfg.show_labels && range < 400.0f) {
                char t[48];
                snprintf(t, sizeof(t), "%d %d", id, (int)(pl->speed * 3.6f));
                dl->AddText(ImVec2(p.x + s + 2, p.y - 6), txt, t);
            }
        }
    }

    /* локальный — в центре */
    dl->AddTriangleFilled(
        ImVec2(center.x, center.y - (float)g_cfg.esp_blip * 1.6f),
        ImVec2(center.x - (float)g_cfg.esp_blip * 1.4f, center.y + (float)g_cfg.esp_blip),
        ImVec2(center.x + (float)g_cfg.esp_blip * 1.4f, center.y + (float)g_cfg.esp_blip),
        lcol);

    /* дальность */
    char t[24];
    snprintf(t, sizeof(t), "%.0fm", range);
    dl->AddText(ImVec2(center.x - 14, center.y + r * 0.55f), txt, t);
}
