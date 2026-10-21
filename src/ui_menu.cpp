/*
 * ui_menu.cpp — ImGui-меню (вкладки: Self / Machines / Players / ESP / Net / Settings / Log)
 */
#include "br_common.h"
#include "detect.h"
#include "input.h"
#include "glapi.h"
#include "ui_gl.h"
#include "ui_menu.h"
#include "ui_esp.h"
#include <imgui.h>

/* ---------------- состояние UI ---------------- */
static bool  g_ctx_ready = false;
static bool  g_uigl_ready = false;
static bool  g_menu_open = true;
static bool  g_radar_open = true;
static bool  g_log_open = false;
static double g_last_frame_ms = 0;

/* горячая кнопка */
static ImVec2 g_btn_px(0, 0);
static bool   g_btn_down = false;
static ImVec2 g_btn_start(0, 0);
static bool   g_btn_moved = false;

/* ---------------- input -> imgui ---------------- */
static void consume_input(void)
{
    ImGuiIO *io = &ImGui::GetIO();
    br_in_ev_t evs[128];
    for (;;) {
        int n = br_input_drain(evs, 128);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            br_in_ev_t *e = &evs[i];
            switch (e->type) {
            case IN_MOUSE_MOVE:  io->AddMousePosEvent(e->x, e->y); break;
            case IN_MOUSE_DOWN:  io->AddMouseButtonEvent(0, true);  break;
            case IN_MOUSE_UP:    io->AddMouseButtonEvent(0, false); break;
            case IN_MOUSE_WHEEL: io->AddMouseWheelEvent(0.0f, e->wheel); break;
            case IN_KEY_DOWN:    io->AddKeyEvent((ImGuiKey)e->key, true);  break;
            case IN_KEY_UP:      io->AddKeyEvent((ImGuiKey)e->key, false); break;
            default: break;
            }
        }
    }
}

/* ---------------- helper ---------------- */
static float dist_to(const float *a, const float *b)
{
    float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static const br_player_t *local_player(void)
{
    int id = g_st.local_id_eff;
    if (id < 0 || id >= BR_MAX_PLAYERS) return 0;
    return g_st.players[id].online ? &g_st.players[id] : 0;
}

/* ---------------- Self tab ---------------- */
static void tab_self(void)
{
    const br_player_t *lp = local_player();

    ImGui::TextDisabled("Локальный игрок");
    if (lp) {
        ImGui::Text("ID: %d    Скорость: %.1f м/с (%.0f км/ч)",
                    lp->id, lp->speed, lp->speed * 3.6f);
        ImGui::Text("Позиция: %.1f / %.1f / %.1f", lp->pos[0], lp->pos[1], lp->pos[2]);
        if (lp->state == 0x13)
            ImGui::TextColored((ImVec4){0.6f,1.0f,0.6f,1}, "В машине: id=%d (%s)",
                               lp->veh_id, lp->veh_class >= 0 ? br_vehicle_class_name(lp->veh_class) : "?");
        else
            ImGui::TextColored((ImVec4){1.0f,0.9f,0.6f,1}, "Пешком");
    } else {
        ImGui::TextColored((ImVec4){1,0.5f,0.5f,1}, "не найден (ожидание данных...)");
    }

    ImGui::Separator();
    ImGui::Text("Локальный ID: %d%s", g_st.local_id, g_st.local_id < 0 ? " (авто)" : "");
    ImGui::SameLine();
    if (ImGui::Button("авто", ImVec2(50, 0))) {
        g_st.local_id = -1;
        g_cfg.local_id = -1;
    }
    {
        static int id_in = -1;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("ID вручную", &id_in)) {
            if (id_in >= 0 && id_in < BR_MAX_PLAYERS) {
                g_st.local_id = id_in;
                g_cfg.local_id = id_in;
                br_feat_queue(FEAT_SET_LOCAL, &g_st.local_id, 4);
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Телепорт (координаты мира)");
    {
        static float tx = 0, ty = 0, tz = 0;
        if (lp && !ImGui::GetIO().WantCaptureKeyboard) {
            /* предзаполнить текущей позицией при первом показе */
            static int prefill = 0;
            if (!prefill && (lp->pos[0] || lp->pos[1] || lp->pos[2])) {
                tx = lp->pos[0]; ty = lp->pos[1]; tz = lp->pos[2];
                prefill = 1;
            }
        }
        ImGui::InputFloat("X", &tx);
        ImGui::InputFloat("Y", &ty);
        ImGui::InputFloat("Z", &tz);
        if (ImGui::Button("Телепорт", ImVec2(140, 0))) {
            float xyz[3] = { tx, ty, tz };
            br_feat_queue(FEAT_TELEPORT, xyz, 12);
        }
    }

    ImGui::Separator();
    ImGui::Text("Турбо (ускорение по вектору движения)");
    {
        bool on = br_feat_turbo_on() != 0;
        if (ImGui::Checkbox("Вкл", &on)) {
            br_feat_set_turbo(on ? 1 : 0, br_feat_turbo_mult());
            g_cfg.turbo_on = on;
        }
        ImGui::SameLine();
        float m = br_feat_turbo_mult();
        ImGui::SetNextItemWidth(140);
        if (ImGui::SliderFloat("x", &m, 1.0f, 100.0f, "%.1f")) {
            br_feat_set_turbo(br_feat_turbo_on() ? 1 : 0, m);
            g_cfg.turbo_mult = m;
        }
    }
}

/* ---------------- Vehicles tab ---------------- */
static int g_sel_veh = -1;

static void tab_vehicles(void)
{
    int n = g_st.vehicles_count;
    ImGui::Text("Машин в пуле: %d / %d", n, BR_MAX_VEHICLES);
    if (!g_st.pool_table)
        ImGui::TextColored((ImVec4){1,0.5f,0.5f,1}, "пул машин не найден");

    if (ImGui::BeginTable("vehs", 6,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("NetID", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Класс", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Водитель", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Дист, м", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Скор", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const br_player_t *lp = local_player();
        float lpos[3] = {0,0,0};
        if (lp) { lpos[0]=lp->pos[0]; lpos[1]=lp->pos[1]; lpos[2]=lp->pos[2]; }

        for (int i = 0; i < n; i++) {
            br_vehicle_t *v = &g_st.vehicles[i];
            float dist = (lp ? dist_to(lpos, v->pos) : 0);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", v->net_id);
            ImGui::TableNextColumn();
            ImGui::Text("%s", br_vehicle_class_name(v->veh_class));
            ImGui::TableNextColumn();
            ImGui::Text("%u", v->occupant_id);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", dist);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", v->speed);
            ImGui::TableNextColumn();
            char idbuf[32];
            snprintf(idbuf, sizeof(idbuf), "##v%u", v->net_id);
            if (ImGui::Selectable(idbuf, g_sel_veh == i, 0, ImVec2(60, 0)))
                g_sel_veh = i;
        }
        ImGui::EndTable();
    }

    if (g_sel_veh >= 0 && g_sel_veh < n) {
        br_vehicle_t *v = &g_st.vehicles[g_sel_veh];
        ImGui::Separator();
        ImGui::Text("Машина %u (%s):", v->net_id, br_vehicle_class_name(v->veh_class));
        if (v->occupant_id) {
            if (ImGui::Button("Телепорт к машине", ImVec2(180, 0))) {
                float xyz[3] = { v->pos[0], v->pos[1], v->pos[2] };
                br_feat_queue(FEAT_TELEPORT, xyz, 12);
            }
        }
        /* двигатель — только если адрес метода известен */
        uintptr_t eng = g_det.tbl ? g_det.tbl->veh_engine_method : 0;
        if (g_det.custom_engine_fn) eng = g_det.custom_engine_fn;
        if (eng) {
            if (ImGui::Button("Двигатель: ON", ImVec2(110, 0)))
                { int a[2] = { g_sel_veh, 1 }; br_feat_queue(FEAT_ENGINE, a, 8); }
            ImGui::SameLine();
            if (ImGui::Button("Двигатель: OFF", ImVec2(110, 0)))
                { int a[2] = { g_sel_veh, 0 }; br_feat_queue(FEAT_ENGINE, a, 8); }
        } else {
            ImGui::TextDisabled("двигатель: адрес метода не известен для сборки");
        }
    }

    ImGui::Separator();
    ImGui::Text("Спавн машины (экспериментально)");
    {
        static int model = 513;
        ImGui::InputInt("Модель (vid)", &model);
        if (ImGui::Button("Создать", ImVec2(110, 0)))
            br_feat_queue(FEAT_SPAWN, &model, 4);
        if (br_feat_last_spawned()) {
            ImGui::SameLine();
            if (ImGui::Button("Сесть", ImVec2(70, 0)))
                br_feat_queue(FEAT_ENTER_SPAWNED, 0, 0);
        }
        ImGui::TextDisabled("фабрика: %s",
            (g_det.tbl && g_det.tbl->create_vehicle) || g_det.custom_create_veh
                ? "адрес есть" : "адрес не известен — укажите в Net");
    }
}

/* ---------------- Players tab ---------------- */
static int g_sel_player = -1;

static void tab_players(void)
{
    int online = g_st.players_online;
    ImGui::Text("Игроков онлайн: %d", online);

    if (ImGui::BeginTable("ply", 6,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("Состояние", ImGuiTableColumnFlags_WidthFixed, 84);
        ImGui::TableSetupColumn("Машина", ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Дист, м", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Скор", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const br_player_t *lp = local_player();
        float lpos[3] = {0,0,0};
        if (lp) { lpos[0]=lp->pos[0]; lpos[1]=lp->pos[1]; lpos[2]=lp->pos[2]; }

        for (int id = 0; id < BR_MAX_PLAYERS; id++) {
            br_player_t *p = &g_st.players[id];
            if (!p->online) continue;
            float dist = (lp ? dist_to(lpos, p->pos) : 0);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d%s", id, (id == g_st.local_id_eff) ? " *" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%s", p->state == 0x13 ? "в машине" : "пешком");
            ImGui::TableNextColumn();
            ImGui::Text("%u", p->veh_id);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", dist);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", p->speed);
            ImGui::TableNextColumn();
            char idbuf[32];
            snprintf(idbuf, sizeof(idbuf), "##p%d", id);
            if (ImGui::Selectable(idbuf, g_sel_player == id, 0, ImVec2(60, 0)))
                g_sel_player = id;
        }
        ImGui::EndTable();
    }

    if (g_sel_player >= 0 && g_st.players[g_sel_player].online) {
        br_player_t *p = &g_st.players[g_sel_player];
        ImGui::Separator();
        ImGui::Text("Игрок %d (поз %.0f/%.0f/%.0f):", p->id, p->pos[0], p->pos[1], p->pos[2]);
        if (g_st.local_id_eff != p->id) {
            if (ImGui::Button("Телепорт к игроку", ImVec2(180, 0))) {
                float xyz[3] = { p->pos[0], p->pos[1], p->pos[2] };
                br_feat_queue(FEAT_TELEPORT, xyz, 12);
            }
        }
        if (ImGui::Button("Сделать локальным", ImVec2(180, 0))) {
            g_st.local_id = p->id;
            g_cfg.local_id = p->id;
            br_feat_queue(FEAT_SET_LOCAL, &g_st.local_id, 4);
        }
    }

    ImGui::Separator();
    ImGui::Text("Мой цвет (таблица цветов, RPC 401)");
    {
        static float col[3] = { 1.0f, 0.35f, 0.35f };
        if (ImGui::ColorEdit3("RGB", col)) {
            br_feat_queue(FEAT_SET_COLOR, col, 12);
        }
        if (!g_st.color_table)
            ImGui::TextDisabled("таблица цветов не найдена");
    }
}

/* ---------------- ESP tab ---------------- */
static void tab_esp(void)
{
    if (ImGui::Checkbox("Радар (ESP)", &g_radar_open)) { /* */ }
    ImGui::SliderFloat("Дальность, м", &g_cfg.radar_range, 50.0f, 2000.0f, "%.0f");
    ImGui::SliderInt("Блипы, px", &g_cfg.esp_blip, 2, 16);
    ImGui::Checkbox("Игроки", &g_cfg.show_players);
    ImGui::Checkbox("Машины", &g_cfg.show_vehicles);
    ImGui::Checkbox("Подписи (id/скор)", &g_cfg.show_labels);
    ImGui::Checkbox("Север (N)", &g_cfg.show_north);

    ImGui::Separator();
    /* превью радара внутри вкладки */
    ImGui::TextDisabled("Предпросмотр (радар рисуется отдельным окном в углу):");
    ImVec2 sz = ImGui::GetContentRegionAvail();
    float r = (sz.x < sz.y ? sz.x : sz.y) * 0.42f;
    if (r > 20) {
        ImVec2 c(ImGui::GetCursorPosX() + r + 8, ImGui::GetCursorPosY() + r + 8);
        ImGui::Dummy(ImVec2(r * 2 + 16, r * 2 + 16));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        br_esp_draw_radar(dl, c, r);
    }
}

/* ---------------- Net tab ---------------- */
static void tab_net(void)
{
    char bid[41] = "неизвестен";
    if (g_det.elf.has_build_id) {
        for (int i = 0; i < 20; i++) sprintf(bid + i * 2, "%02x", g_det.elf.build_id[i]);
    }
    ImGui::Text("База игры:   0x%lx", (unsigned long)g_det.base);
    ImGui::Text("Build-ID:   %s", bid);
    ImGui::Text("Сборка:     %s", g_st.build ? br_build_name(g_st.build) : "не определена");
    ImGui::Text("Пул машин:  %s", g_det.src_pool[0] ? g_det.src_pool : "—");
    ImGui::Text("Менеджер:   %s", g_det.src_mgr[0] ? g_det.src_mgr : "—");
    ImGui::Text("Педы:       %s", g_det.src_ped[0] ? g_det.src_ped : "—");
    if (g_det.probe.pool_found)
        ImGui::TextDisabled("проба: pool=%d слотов, mgr=%d, online=%d",
                            g_det.probe.pool_valid_slots, g_det.probe.mgr_valid_ids,
                            g_det.probe.players_online);

    if (ImGui::Button("Rescan (повторный поиск)", ImVec2(220, 0)))
        br_feat_queue(FEAT_RESCAN, 0, 0);

    ImGui::Separator();
    ImGui::Text("Синхро-монитор (пакетов/сек)");
    if (g_mon.running) {
        static const int ids[] = { 11, 14, 19, 29, 30, 32, 33, 34, 38, 39 };
        static const char *names[] = {
            "RPC-канал", "служ. 14", "служ. 19", "смежный 29",
            "sync игрока", "sync машин", "связка", "InCar", "OnFoot", "Crane"
        };
        for (int i = 0; i < (int)(int)(sizeof(ids)/sizeof(ids[0])); i++) {
            int id = ids[i];
            ImGui::Text("  %-10s %3d: %6.1f/s  (всего %llu)",
                        names[i], id, g_mon.pps[id], (unsigned long long)g_mon.total[id]);
        }
    } else {
        ImGui::TextDisabled("недоступен для этой сборки (глобал packetId не выделен)");
    }

    ImGui::Separator();
    ImGui::Text("Справочник протокола (из RE-документации)");
    if (ImGui::CollapsingHeader("Синхро-пакеты", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("30 — основной синк игрока (поз+кватернион+скорость), ~15-20 Гц");
        ImGui::BulletText("32 — синк машины: 68 Б (5×96 бит: поз/поворот/скорость...)");
        ImGui::BulletText("33 — связка игрок↔машина, 26 Б, только S2C, ~2.3 Гц");
        ImGui::BulletText("34 — InCar: id машины, кватернион, поз, скорость, здоровье");
        ImGui::BulletText("38 — OnFoot (31 Б)");
        ImGui::BulletText("39 — кран/порт (56 Б, SetCraneRopeLength)");
        ImGui::BulletText("11 — RPC-канал (125 обработчиков, id 302-457)");
    }

    ImGui::Separator();
    ImGui::Text("Ручные адреса (VA в .so; 0 = выкл)");
    {
        #define HEXFIELD(label, field) \
            { char buf[24]; snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)g_det.field); \
              if (ImGui::InputText(label, buf, sizeof(buf))) { \
                  unsigned long long v = strtoull(buf, 0, 0); \
                  g_det.field = (uintptr_t)v; \
                  br_feat_queue(FEAT_RESCAN, 0, 0); } }
        HEXFIELD("pool glob",        custom_pool)
        HEXFIELD("player mgr",       custom_mgr)
        HEXFIELD("ped table",        custom_ped)
        HEXFIELD("color table",      custom_color)
        HEXFIELD("dispatcher",       custom_dispatcher)
        HEXFIELD("CreateVehicle",    custom_create_veh)
        HEXFIELD("engine method",    custom_engine_fn)
        #undef HEXFIELD
    }
}

/* ---------------- Settings tab ---------------- */
static void tab_settings(void)
{
    ImGui::Text("Сборка: %s", br_build_name(g_cfg.build));
    {
        const char *items[] = { "Авто", "16.106.17587", "16.110.18323" };
        if (ImGui::Combo("Определение", &g_cfg.build, items, 3))
            br_feat_queue(FEAT_RESCAN, 0, 0);
    }
    ImGui::SliderFloat("Прозрачность меню", &g_cfg.ui_opacity, 0.3f, 1.0f, "%.2f");
    ImGui::SliderFloat("Масштаб", &g_cfg.menu_scale, 0.7f, 2.0f, "%.2f");
    {
        static const char *ll[] = { "off", "error", "warn", "info" };
        if (ImGui::Combo("Лог", &g_cfg.log_level, ll, 4)) { /* применяется */ }
    }
    ImGui::Checkbox("Окно лога", &g_log_open);
    ImGui::Separator();
    if (ImGui::Button("Сохранить настройки", ImVec2(200, 0)))
        br_cfg_save();
    ImGui::SameLine();
    if (ImGui::Button("Сброс", ImVec2(80, 0))) {
        br_cfg_init();
        g_st.local_id = g_cfg.local_id;
    }
    ImGui::TextDisabled("файл: %s", br_cfg_path());
}

/* ---------------- Log tab ---------------- */
static void tab_log(void)
{
    char text[16 * 1024];
    char lvl[256];
    int n = br_log_read(text, sizeof(text), 256, lvl);
    const char *p = text;
    ImGui::BeginChild("logchild", ImVec2(0, 0), 0, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (int i = 0; i < n; i++) {
        ImU32 c = IM_COL32(240, 240, 240, 255);
        if (lvl[i] == 'E') c = IM_COL32(255, 85, 85, 255);
        else if (lvl[i] == 'W') c = IM_COL32(255, 224, 106, 255);
        /* строка до \n */
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        ImVec4 col(((c & 0xFF) / 255.0f), (((c >> 8) & 0xFF) / 255.0f), (((c >> 16) & 0xFF) / 255.0f), 1.0f);
        ImGui::TextColored(col, "%.*s", len, p);
        if (!eol) break;
        p = eol + 1;
    }
    ImGui::EndChild();
}

/* ---------------- горячая кнопка ---------------- */
static void hot_button(float w, float h)
{
    g_btn_px = ImVec2(g_cfg.btn_x * w, g_cfg.btn_y * h);
    ImGuiIO *io = &ImGui::GetIO();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    float r = 22.0f;
    dl->AddCircleFilled(g_btn_px, r, 0x991B3A5F);
    dl->AddCircle(g_btn_px, r, 0xFF4FA3E3, 0, 2.0f);
    dl->AddText(ImVec2(g_btn_px.x - 6.0f, g_btn_px.y - 6.0f), 0xFFFFFFFF, "BR");

    ImVec2 m(io->MousePos.x, io->MousePos.y);
    bool over = (m.x - g_btn_px.x) * (m.x - g_btn_px.x) +
                (m.y - g_btn_px.y) * (m.y - g_btn_px.y) <= r * r;

    if (io->MouseClicked[0] && over) {
        g_btn_down = true;
        g_btn_start = m;
        g_btn_moved = false;
    }
    if (io->MouseDown[0] && g_btn_down) {
        ImVec2 d(m.x - g_btn_start.x, m.y - g_btn_start.y);
        if (d.x*d.x + d.y*d.y > 64) g_btn_moved = true;
        if (g_btn_moved) {
            g_btn_px = ImVec2(m.x, m.y);
            g_cfg.btn_x = (float)(g_btn_px.x / w);
            g_cfg.btn_y = (float)(g_btn_px.y / h);
        }
    }
    if (!io->MouseDown[0] && g_btn_down) {
        g_btn_down = false;
        if (!g_btn_moved) g_menu_open = !g_menu_open;
    }
}

/* ---------------- окно-меню ---------------- */
static void draw_menu(float w, float h)
{
    ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Menu-BR — Black Russia", &g_menu_open, flags);
    if (ImGui::BeginTabBar("##main", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Свой"))       { tab_self();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Машины"))     { tab_vehicles(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Игроки"))     { tab_players();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("ESP"))        { tab_esp();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Net"))        { tab_net();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Settings"))   { tab_settings(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Лог"))        { tab_log();      ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void draw_radar(float w, float h)
{
    if (!g_radar_open) return;
    float r = 90.0f;
    ImGui::SetNextWindowSize(ImVec2(r * 2 + 24, r * 2 + 24), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(w - r * 2 - 36, 24), ImGuiCond_Always);
    ImGui::Begin("ESP##radar", 0,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 c(r + 12, r + 12);
    br_esp_draw_radar(dl, c, r);
    ImGui::End();
}

/* ---------------- кадр ---------------- */
void br_ui_init(void)
{
    if (g_ctx_ready) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO *io = &ImGui::GetIO();
    io->IniFilename = NULL; /* не пишем imgui.ini в процесс игры */
    io->FontDefault = io->Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle *st = &ImGui::GetStyle();
    st->WindowRounding = 8.0f;
    st->ChildRounding = 6.0f;
    st->FrameRounding = 6.0f;
    g_ctx_ready = true;
}

void br_frame_render_ui(void)
{
    if (!g_gl.gl_ok) return;
    if (g_gl.eglGetCurrentContext && !g_gl.eglGetCurrentContext())
        return; /* не в GL-контексте */

    if (!g_ctx_ready) br_ui_init();

    double now = br_now_ms();
    double dt = g_last_frame_ms ? (now - g_last_frame_ms) : 16.0;
    g_last_frame_ms = now;

    consume_input();

    int vp[4] = {0, 0, 1280, 720};
    if (g_gl.glGetIntegerv) g_gl.glGetIntegerv(0x0BA2, vp);
    int w = vp[2] > 0 ? vp[2] : 1280;
    int h = vp[3] > 0 ? vp[3] : 720;

    if (!g_uigl_ready) {
        if (br_uigl_init(w, h)) g_uigl_ready = true;
        else return;
    }

    ImGui::NewFrame();

    /* фоновые задачи на GL-треде */
    br_feat_turbo_tick(dt);
    br_feat_drain();

    draw_menu(w, h);
    draw_radar(w, h);

    /* горячая кнопка — в DrawList ДО Render() */
    hot_button((float)w, (float)h);

    ImGui::Render();
    br_uigl_render(w, h);
}
