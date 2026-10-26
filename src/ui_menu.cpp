/*
 * ui_menu.cpp — меню (редизайн 16.111):
 *   - плавающий pill-бейдж (открытие/закрытие, HUD, FPS) — перетаскиваемый,
 *   - мини-HUD (телеметрия локального игрока + сети),
 *   - главное окно с вкладками, тёмная тема с cyan-акцентом,
 *   - ui_scale (авто по высоте экрана или ручной), адаптивные размеры,
 *   - кириллица через системный Roboto/Noto.
 */
#include "br_common.h"
#include "detect.h"
#include "input.h"
#include "glapi.h"
#include "ui_gl.h"
#include "ui_menu.h"
#include "ui_esp.h"
#include "netmon.h"
#include <sys/stat.h>
#include <imgui.h>
#include <imgui_internal.h>

/* ---------------- состояние UI ---------------- */
static bool   g_ctx_ready  = false;
static bool   g_uigl_ready = false;
static bool   g_menu_open  = true;
static bool   g_radar_open = true;
static double g_last_frame_ms = 0;

/* собственный изолированный ImGui-контекст (принцип инспектора 16.111:
 * без зависимости от встроенного в игру GImGui) */
static ImGuiContext *g_own_ctx = 0;
static void         *g_last_egl_ctx = 0;
static int           g_in_frame = 0;   /* re-entrancy guard */

/* хит-рект (для swallow кликов, 16.111) — копируем после каждого кадра
 * из списка активных окон ImGui (как в инспекторе) */
#define UI_RECT_MAX 12
static float  g_win_rects[UI_RECT_MAX][4];
static int    g_win_rect_cnt = 0;

/* ---------------- input -> imgui ---------------- */
static void consume_input(void)
{
    ImGuiIO *io = &ImGui::GetIO();
    br_in_ev_t evs[128];
    for (;;) {
        int n = br_input_drain(evs, 128);
        if (n <= 0) break;
        io->AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
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

/* ---------------- helpers ---------------- */
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

static bool player_in_veh(const br_player_t *p)
{
    if (g_st.pool_v2) return p->state != 0;
    return p->state == 0x13;
}

static float ui_scale_now(int h)
{
    if (g_cfg.menu_scale > 0.0f) return g_cfg.menu_scale;
    float s = (float)h / 1080.0f;
    if (s < 0.85f) s = 0.85f;
    if (s > 2.0f)  s = 2.0f;
    return s;
}

/* ---------------- тема ---------------- */
static void apply_theme(void)
{
    ImGuiStyle *st = &ImGui::GetStyle();
    ImGui::StyleColorsDark(st);
    ImVec4 *c = st->Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.06f, 0.08f, 0.11f, 0.95f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.08f, 0.10f, 0.14f, 0.60f);
    c[ImGuiCol_PopupBg]        = ImVec4(0.08f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Border]         = ImVec4(0.16f, 0.22f, 0.30f, 0.70f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.24f, 0.33f, 1.00f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.20f, 0.30f, 0.42f, 1.00f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.05f, 0.07f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.16f, 0.24f, 1.00f);
    c[ImGuiCol_CheckMark]      = ImVec4(0.22f, 0.78f, 0.95f, 1.00f);
    c[ImGuiCol_SliderGrab]     = ImVec4(0.22f, 0.78f, 0.95f, 0.85f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.22f, 0.84f, 0.96f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.13f, 0.20f, 0.29f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.20f, 0.42f, 0.55f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.22f, 0.55f, 0.70f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.15f, 0.25f, 0.36f, 0.80f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.35f, 0.50f, 0.80f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.22f, 0.42f, 0.62f, 1.00f);
    c[ImGuiCol_Tab]            = ImVec4(0.10f, 0.15f, 0.22f, 1.00f);
    c[ImGuiCol_TabHovered]     = ImVec4(0.22f, 0.55f, 0.70f, 0.80f);
    c[ImGuiCol_TabActive]      = ImVec4(0.16f, 0.30f, 0.42f, 1.00f);
    c[ImGuiCol_Separator]      = ImVec4(0.20f, 0.28f, 0.38f, 0.70f);
    c[ImGuiCol_Text]           = ImVec4(0.88f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.45f, 0.52f, 0.60f, 1.00f);
    st->WindowRounding  = 10.0f;
    st->ChildRounding   = 8.0f;
    st->FrameRounding   = 6.0f;
    st->GrabRounding    = 6.0f;
    st->PopupRounding   = 8.0f;
    st->ScrollbarRounding= 8.0f;
    st->TabRounding     = 6.0f;
    st->WindowPadding   = ImVec2(14, 12);
    st->ItemSpacing     = ImVec2(8, 7);
}

/* ---------------- шрифт (системный, кириллица) ---------------- */
static bool font_readable(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && st.st_size > 1024;
}

/* шрифты как в инспекторе 16.111: системный TTF, кириллица, oversample */
static void load_fonts(float size)
{
    ImGuiIO *io = &ImGui::GetIO();
    io->Fonts->Clear();
    static const char *paths[] = {
        "/system/fonts/Roboto-Medium.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
    };
    ImFont *f = 0;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    for (int i = 0; i < 4 && !f; i++) {
        if (font_readable(paths[i]))
            f = io->Fonts->AddFontFromFileTTF(paths[i], size, &cfg,
                                              io->Fonts->GetGlyphRangesCyrillic());
    }
    if (!f) {
        f = io->Fonts->AddFontDefault();
        LOGW("ui: системный TTF не найден, default (без кириллицы)");
    } else {
        LOGI("ui: шрифт %s %.1fpx", paths[0], size);
    }
    io->FontDefault = f;
}

static void br_ui_init_ex(int w, int h)
{
    if (g_ctx_ready) return;
    IMGUI_CHECKVERSION();

    /* собственный контекст (изолирован от встроенного imgui игры) */
    g_own_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_own_ctx);
    ImGuiIO *io = &ImGui::GetIO();
    io->IniFilename = NULL;          /* не пишем imgui.ini в процесс игры */
    io->LogFilename = NULL;
    io->ConfigInputTrickleEventQueue = true;
    io->DisplaySize = ImVec2((float)w, (float)h);

    /* размер шрифта как в инспекторе: clamp(short_side/38, 18, 28) */
    int short_side = w < h ? w : h;
    if (short_side < 480) short_side = 480;
    float fs = (float)short_side / 38.0f;
    if (fs < 18.0f) fs = 18.0f;
    if (fs > 28.0f) fs = 28.0f;
    load_fonts(fs);
    apply_theme();
    g_ctx_ready = true;
    LOGI("ui: собственный ImGui-контекст готов (%p)", (void *)g_own_ctx);
}

void br_ui_ctx_destroy(void)
{
    if (g_own_ctx) {
        ImGui::SetCurrentContext(g_own_ctx);
        ImGui::DestroyContext();
        g_own_ctx = 0;
    }
    g_ctx_ready = false;
}

void br_ui_init(void)
{
    br_ui_init_ex(1280, 720);
}

/* ---------------- pill (плавающий бейдж) ---------------- */
static void draw_pill(int w, int h, float s)
{
    ImGui::SetNextWindowPos(ImVec2(g_cfg.btn_x * w, g_cfg.btn_y * h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin("##BRPill", 0, flags)) { ImGui::End(); return; }

    const ImVec4 accent = g_menu_open ? ImVec4(0.22f, 0.84f, 0.96f, 1.0f)
                                      : ImVec4(0.65f, 0.75f, 0.85f, 1.0f);
    ImGui::TextColored(accent, "BR %s", br_build_name(g_st.build));
    ImGui::SameLine();
    if (ImGui::Button(g_menu_open ? "Скрыть" : "Меню", ImVec2(74*s, 0)))
        g_menu_open = !g_menu_open;
    ImGui::SameLine();
    if (ImGui::SmallButton(g_cfg.show_mini_hud ? "HUD:ВКЛ" : "HUD")) {
        g_cfg.show_mini_hud = !g_cfg.show_mini_hud;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| %.0f FPS | П:%d М:%d",
                        ImGui::GetIO().Framerate, g_st.players_online, g_st.vehicles_count);
    ImGui::End();
}

/* ---------------- мини-HUD ---------------- */
static void draw_mini_hud(float s)
{
    ImGui::SetNextWindowPos(ImVec2(24.0f * s, 84.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##BRMiniHud", 0, flags)) { ImGui::End(); return; }

    ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.98f, 1.0f),
                       "Black Russia %s", br_build_name(g_st.build));
    ImGui::Separator();

    const br_player_t *lp = local_player();
    if (lp) {
        ImGui::Text("Игрок: %d%s", lp->id, lp->name[0] ? lp->name : "");
        if (lp->hp >= 0) {
            ImVec4 hc = lp->hp > 25 ? ImVec4(0.4f, 0.9f, 0.5f, 1) : ImVec4(1, 0.4f, 0.4f, 1);
            ImGui::TextColored(hc, "HP %.0f / Armor %.0f", lp->hp, lp->armor);
        }
        ImGui::Text("XYZ: %.0f / %.0f / %.0f", lp->pos[0], lp->pos[1], lp->pos[2]);
        if (player_in_veh(lp))
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1),
                               "Авто #%u (%s): %.0f км/ч", lp->veh_id,
                               lp->veh_class >= 0 ? br_vehicle_class_name(lp->veh_class) : "?",
                               lp->speed * 3.6f);
        else
            ImGui::Text("Пешком: %.0f км/ч", lp->speed * 3.6f);
    } else {
        ImGui::TextDisabled("локальный игрок не найден");
    }

    ImGui::Separator();
    ImGui::Text("Онлайн: %d   Машин: %d", g_st.players_online, g_st.vehicles_count);

    if (g_st.pool_v2) {
        int ping = br_netmon_ping_avg();
        int mtu = br_netmon_mtu();
        if (ping >= 0)
            ImGui::Text("Сеть: ping %d мс | MTU %d | %s", ping, mtu,
                        br_netmon_connected() ? "онлайн" : "откл.");
        else
            ImGui::TextDisabled("Сеть: данные появятся в лобби");
    }
    ImGui::End();
}

/* ---------------- Self tab ---------------- */
static void tab_self(void)
{
    const br_player_t *lp = local_player();

    ImGui::TextDisabled("Локальный игрок");
    if (lp) {
        ImGui::Text("ID: %d%s", lp->id, lp->name[0] ? lp->name : "");
        ImGui::Text("Скорость: %.1f м/с (%.0f км/ч)", lp->speed, lp->speed * 3.6f);
        ImGui::Text("Позиция: %.1f / %.1f / %.1f", lp->pos[0], lp->pos[1], lp->pos[2]);
        if (lp->hp >= 0)
            ImGui::Text("HP: %.0f   Armor: %.0f", lp->hp, lp->armor);
        if (player_in_veh(lp))
            ImGui::TextColored((ImVec4){0.6f,1.0f,0.6f,1}, "В машине: #%u (%s)",
                               lp->veh_id,
                               lp->veh_class >= 0 ? br_vehicle_class_name(lp->veh_class) : "?");
        else
            ImGui::TextColored((ImVec4){1.0f,0.9f,0.6f,1}, "Пешком");
    } else {
        ImGui::TextColored((ImVec4){1,0.5f,0.5f,1}, "не найден (ожидание данных...)");
    }

    if (g_st.pool_v2) {
        ImGui::Separator();
        if (ImGui::Button("Лечить (HP/Armor = 100)", ImVec2(220, 0))) {
            float hv[2] = { 100.0f, 100.0f };
            br_feat_queue(FEAT_HEAL, hv, 8);
        }
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
        static int prefill = 0;
        if (lp && !prefill && (lp->pos[0] || lp->pos[1] || lp->pos[2])) {
            tx = lp->pos[0]; ty = lp->pos[1]; tz = lp->pos[2];
            prefill = 1;
        }
        if (ImGui::Button("Текущие")) {
            if (lp) { tx = lp->pos[0]; ty = lp->pos[1]; tz = lp->pos[2]; }
        }
        ImGui::SameLine();
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
        if (g_st.pool_v2)
            ImGui::TextDisabled("режим: удержание скорости (vel-крюиз)");
    }

    ImGui::Separator();
    ImGui::Text("Мой цвет (таблица цветов)");
    if (g_st.color_table) {
        static float col[3] = { 1.0f, 0.35f, 0.35f };
        if (ImGui::ColorEdit3("RGB", col))
            br_feat_queue(FEAT_SET_COLOR, col, 12);
    } else {
        ImGui::TextDisabled("таблица цветов не найдена для сборки");
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

    if (n > 0 && ImGui::BeginTable("vehs", 6,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
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
        ImGui::Text("Машина #%u (%s):", v->net_id, br_vehicle_class_name(v->veh_class));
        if (ImGui::Button("Телепорт к машине", ImVec2(180, 0))) {
            float xyz[3] = { v->pos[0], v->pos[1], v->pos[2] };
            br_feat_queue(FEAT_TELEPORT, xyz, 12);
        }
        if (g_st.pool_v2) {
            ImGui::SameLine();
            if (ImGui::Button("Сесть", ImVec2(80, 0)))
                br_feat_queue(FEAT_ENTER_NETID, &v->net_id, 4);
        }
        uintptr_t eng = g_det.tbl ? g_det.tbl->veh_engine_method : 0;
        if (g_det.custom_engine_fn) eng = g_det.custom_engine_fn;
        if (eng) {
            ImGui::SameLine();
            if (ImGui::Button("Двигатель: ON", ImVec2(110, 0)))
                { int a[2] = { g_sel_veh, 1 }; br_feat_queue(FEAT_ENGINE, a, 8); }
            ImGui::SameLine();
            if (ImGui::Button("Двигатель: OFF", ImVec2(110, 0)))
                { int a[2] = { g_sel_veh, 0 }; br_feat_queue(FEAT_ENGINE, a, 8); }
        } else if (g_st.pool_v2) {
            ImGui::TextDisabled("двигатель: метод не установлен для этой сборки");
        }
    }

    ImGui::Separator();
    if (!g_st.pool_v2) {
        ImGui::Text("Спавн машины (экспериментально)");
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
    } else {
        ImGui::TextDisabled("спавн машин: для этой сборки метод не установлен");
    }
}

/* ---------------- Players tab ---------------- */
static int g_sel_player = -1;

static void tab_players(void)
{
    int online = g_st.players_online;
    ImGui::Text("Игроков онлайн: %d", online);

    if (online > 0 && ImGui::BeginTable("ply", 7,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("Ник", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Состояние", ImGuiTableColumnFlags_WidthFixed, 84);
        ImGui::TableSetupColumn("Машина", ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Дист, м", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Скор", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.6f);
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
            ImGui::Text("%s", p->name[0] ? p->name : "—");
            ImGui::TableNextColumn();
            ImGui::Text("%s", player_in_veh(p) ? "в машине" : "пешком");
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
        ImGui::Text("Игрок %d%s (поз %.0f/%.0f/%.0f):", p->id,
                    p->name[0] ? p->name : "", p->pos[0], p->pos[1], p->pos[2]);
        if (g_st.local_id_eff != p->id) {
            if (ImGui::Button("Телепорт к игроку", ImVec2(180, 0))) {
                float xyz[3] = { p->pos[0], p->pos[1], p->pos[2] };
                br_feat_queue(FEAT_TELEPORT, xyz, 12);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Сделать локальным", ImVec2(180, 0))) {
            g_st.local_id = p->id;
            g_cfg.local_id = p->id;
            br_feat_queue(FEAT_SET_LOCAL, &g_st.local_id, 4);
        }
    }
}

/* ---------------- ESP tab ---------------- */
static void tab_esp(void)
{
    if (ImGui::Checkbox("Радар (ESP)", &g_radar_open)) { /* */ }
    if (ImGui::Checkbox("Мини-HUD", &g_cfg.show_mini_hud)) { /* */ }
    ImGui::SliderFloat("Дальность, м", &g_cfg.radar_range, 50.0f, 2000.0f, "%.0f");
    ImGui::SliderInt("Блипы, px", &g_cfg.esp_blip, 2, 16);
    ImGui::Checkbox("Игроки", &g_cfg.show_players);
    ImGui::Checkbox("Машины", &g_cfg.show_vehicles);
    ImGui::Checkbox("Подписи (id/скор)", &g_cfg.show_labels);
    ImGui::Checkbox("Север (N)", &g_cfg.show_north);

    ImGui::Separator();
    ImGui::TextDisabled("Радар рисуется отдельным окном в углу экрана:");
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

/* справочник raw-пакетов 16.111 (из RE-анализа) */
static const char *net_name_16111(int kind, int id)
{
    if (kind == 0) {
        switch (id) {
        case 27: return "string-query";
        case 29: return "event(41)";
        case 30: return "player-state 73B";
        case 31: return "local-sync 55B";
        case 32: return "veh-state 68B";
        case 33: return "diff-state 27B";
        case 34: return "InCar-veh 66B";
        case 35: return "slot-diff";
        case 37: return "periodic 19B";
        case 38: return "onfoot 32B";
        case 39: return "crane 57B";
        }
    }
    if (kind == 1) {
        switch (id) {
        case 314: case 320: case 324: case 342: case 355: case 356:
        case 357: case 361: case 371: case 374: case 378: case 403:
        case 414: case 423: case 436: case 437: case 441: return "strict-RPC";
        case 413: return "RPC-413 (частый)";
        }
    }
    return 0;
}

static void tab_net(void)
{
    char bid[41] = "неизвестен";
    if (g_det.elf.has_build_id) {
        for (int i = 0; i < 20; i++) sprintf(bid + i * 2, "%02x", g_det.elf.build_id[i]);
    }
    ImGui::Text("База игры:   0x%lx", (unsigned long)g_det.base);
    ImGui::Text("Build-ID:   %s", bid);
    ImGui::Text("Сборка:     %s", g_st.build ? br_build_name(g_st.build) : "не определена");
    ImGui::Text("Пул машин:  %s   Менеджер: %s   Педы: %s",
                g_det.src_pool[0] ? g_det.src_pool : "—",
                g_det.src_mgr[0]  ? g_det.src_mgr  : "—",
                g_det.src_ped[0]  ? g_det.src_ped  : "—");
    if (g_det.probe.pool_found)
        ImGui::TextDisabled("проба: pool=%d, mgr=%d, online=%d",
                            g_det.probe.pool_valid_slots, g_det.probe.mgr_valid_ids,
                            g_det.probe.players_online);

    ImGui::Separator();

    /* 16.111: живой монитор (хуки Send/RPC) */
    if (g_st.pool_v2) {
        int ping = br_netmon_ping_avg();
        int mtu = br_netmon_mtu();
        if (ping >= 0)
            ImGui::Text("RakClient: ping %d мс | MTU %d | %s",
                        ping, mtu, br_netmon_connected() ? "подключён" : "не подключён");
        else
            ImGui::TextDisabled("RakClient: данные появятся в лобби");
        ImGui::Text("Всего: Send %llu | RPC %llu",
                    (unsigned long long)br_netmon_total(0),
                    (unsigned long long)br_netmon_total(1));

        if (ImGui::CollapsingHeader("Пакеты (счётчики)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("nids", 4, ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, 160))) {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Тип");
                ImGui::TableSetupColumn("Всего");
                ImGui::TableSetupColumn("Название");
                ImGui::TableHeadersRow();
                for (int id = 0; id < 600; id++) {
                    uint64_t cs = br_netmon_count(0, id);
                    uint64_t cr = br_netmon_count(1, id);
                    if (!cs && !cr) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", id);
                    ImGui::TableNextColumn(); ImGui::Text(cs ? "Send" : "RPC");
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", (unsigned long long)(cs ? cs : cr));
                    ImGui::TableNextColumn();
                    const char *nm = net_name_16111(cs ? 0 : 1, id);
                    ImGui::Text("%s", nm ? nm : "—");
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Последние события")) {
            net_event_t evs[14];
            int ne = br_netmon_snapshot(evs, 14);
            for (int i = 0; i < ne; i++) {
                char hex[64] = "";
                for (int b = 0; b < 8 && b < (evs[i].size > 0); b++)
                    sprintf(hex + strlen(hex), "%02x ", evs[i].head[b]);
                const char *nm = net_name_16111(evs[i].kind, evs[i].id);
                ImGui::Text("%s %-11d %3uБ  %uмс  %s%s",
                            evs[i].kind ? "RPC" : "SEND",
                            evs[i].id, evs[i].size, evs[i].t,
                            nm ? nm : "", hex);
            }
            if (!ne) ImGui::TextDisabled("событий пока нет");
        }
    }

    /* legacy: сэмплирующий sync-монитор (16.110/16.106) */
    if (!g_st.pool_v2) {
        ImGui::Text("Синхро-монитор (пакетов/сек)");
        if (g_mon.running) {
            static const int ids[] = { 11, 14, 19, 29, 30, 32, 33, 34, 38, 39 };
            static const char *names[] = {
                "RPC-канал", "служ. 14", "служ. 19", "смежный 29",
                "sync игрока", "sync машин", "связка", "InCar", "OnFoot", "Crane"
            };
            for (int i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
                int id = ids[i];
                ImGui::Text("  %-10s %3d: %6.1f/s  (всего %llu)",
                            names[i], id, g_mon.pps[id], (unsigned long long)g_mon.total[id]);
            }
        } else {
            ImGui::TextDisabled("недоступен для этой сборки (глобал packetId не выделен)");
        }
    }

    ImGui::Separator();
    ImGui::Text("Справочник протокола");
    if (g_st.pool_v2) {
        if (ImGui::CollapsingHeader("Raw 16.111", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("27 — строковые запросы/команды (4 callsite)");
            ImGui::BulletText("29 — событие, 41 Б (invalid victim check)");
            ImGui::BulletText("30 — state/sync LocalPlayer, 73 Б (основной)");
            ImGui::BulletText("31 — local sync helper, 55 Б");
            ImGui::BulletText("32 — многовекторное vehicle-состояние, 68 Б");
            ImGui::BulletText("33 — дифф кэша игрока/авто, 27 Б");
            ImGui::BulletText("34 — InCar vehicle sync, 66 Б");
            ImGui::BulletText("35 — дифф по слотам (оружие/инвентарь)");
            ImGui::BulletText("37 — периодическая структура, 19 Б, rate 101 мс");
            ImGui::BulletText("38 — LocalPlayer onfoot, 32 Б (byteWeaponState)");
            ImGui::BulletText("39 — транспорт/кран, 57 Б");
        }
        if (ImGui::CollapsingHeader("RPC 16.111")) {
            ImGui::BulletText("строгие: 314 320 324 342 355 356 357 361 371 374 378");
            ImGui::BulletText("        403 413 414 423 436 437 441");
            ImGui::BulletText("413 — самый частый (43 strict sites)");
        }
    } else {
        if (ImGui::CollapsingHeader("Синхро-пакеты", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("30 — основной синк игрока (поз+кватернион+скорость), ~15-20 Гц");
            ImGui::BulletText("32 — синк машины: 68 Б (5×96 бит: поз/поворот/скорость...)");
            ImGui::BulletText("33 — связка игрок↔машина, 26 Б, только S2C, ~2.3 Гц");
            ImGui::BulletText("34 — InCar: id машины, кватернион, поз, скорость, здоровье");
            ImGui::BulletText("38 — OnFoot (31 Б)");
            ImGui::BulletText("39 — кран/порт (56 Б, SetCraneRopeLength)");
            ImGui::BulletText("11 — RPC-канал (125 обработчиков, id 302-457)");
        }
    }

    ImGui::Separator();
    if (!g_st.pool_v2) {
        ImGui::Text("Ручные адреса (VA в .so; 0 = выкл)");
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

    if (ImGui::Button("Rescan (повторный поиск)", ImVec2(220, 0)))
        br_feat_queue(FEAT_RESCAN, 0, 0);
}

/* ---------------- Settings tab ---------------- */
static void tab_settings(void)
{
    ImGui::Text("Сборка: %s", br_build_name(g_cfg.build));
    {
        const char *items[] = { "Авто", "16.106.17587", "16.110.18323",
                                "16.111.19050", "Ручной (Custom)" };
        if (ImGui::Combo("Определение", &g_cfg.build, items, 5))
            br_feat_queue(FEAT_RESCAN, 0, 0);
    }
    ImGui::SliderFloat("Прозрачность меню", &g_cfg.ui_opacity, 0.3f, 1.0f, "%.2f");
    ImGui::SliderFloat("Масштаб UI (0 = авто)", &g_cfg.menu_scale, 0.0f, 2.0f, "%.2f");
    {
        static const char *ll[] = { "off", "error", "warn", "info" };
        if (ImGui::Combo("Лог", &g_cfg.log_level, ll, 4)) { /* применяется */ }
    }
    ImGui::Separator();
    if (ImGui::Button("Сохранить настройки", ImVec2(200, 0)))
        br_cfg_save();
    ImGui::SameLine();
    if (ImGui::Button("Сброс", ImVec2(80, 0))) {
        br_cfg_init();
        g_st.local_id = g_cfg.local_id;
    }
    ImGui::TextDisabled("файл: %s", br_cfg_path());
    if (ImGui::Button("Выключить меню (завершение)", ImVec2(260, 0)))
        br_shutdown_request();
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
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        ImVec4 col(((c & 0xFF) / 255.0f), (((c >> 8) & 0xFF) / 255.0f), (((c >> 16) & 0xFF) / 255.0f), 1.0f);
        ImGui::TextColored(col, "%.*s", len, p);
        if (!eol) break;
        p = eol + 1;
    }
    ImGui::EndChild();
}

/* ---------------- главное окно ---------------- */
static void draw_menu(int w, int h, float s)
{
    float def_w = 980.0f * s;
    float aw = w * 0.88f; if (aw < 540.0f) aw = 540.0f;
    if (def_w > aw) def_w = aw;
    float def_h = 640.0f * s;
    float ah = h * 0.82f; if (ah < 380.0f) ah = 380.0f;
    if (def_h > ah) def_h = ah;

    ImGui::SetNextWindowSize(ImVec2(def_w, def_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60.0f * s, 90.0f * s), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Menu-BR — Black Russia", &g_menu_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##main", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Свой"))     { tab_self();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Машины"))   { tab_vehicles(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Игроки"))   { tab_players();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("ESP"))      { tab_esp();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Net"))      { tab_net();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Настройки")){ tab_settings(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Лог"))      { tab_log();      ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

/* ---------------- радар ---------------- */
static void draw_radar(float s)
{
    if (!g_radar_open) return;
    float r = 90.0f * s;
    ImGui::SetNextWindowSize(ImVec2(r * 2 + 24, r * 2 + 24), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - r * 2 - 36,
                                   24.0f * s), ImGuiCond_Always);
    ImGui::Begin("ESP##radar", 0,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 c(r + 12, r + 12);
    br_esp_draw_radar(dl, c, r);
    ImGui::End();
}

/* ---------------- кадр (принцип инспектора 16.111) ---------------- */
void br_frame_render_ui(void)
{
    if (g_in_frame) return;           /* re-entrancy guard */
    g_in_frame = 1;

    if (!g_gl.gl_ok) { g_in_frame = 0; return; }

    void *cur_ctx = br_uigl_current_egl_context();
    if (!cur_ctx) {
        static int noctx = 0;
        if (++noctx == 1) LOGW("ui: нет текущего EGL-контекста (оверлей ждёт GLThread)");
        g_in_frame = 0;
        return;
    }

    /* размер поверхности: eglQuerySurface → BSS-глобал → viewport */
    int w = 0, h = 0;
    uintptr_t wh_va = (g_det.tbl && g_det.tbl->surface_wh_glob)
        ? (g_det.base + g_det.tbl->surface_wh_glob) : 0;
    if (!br_uigl_surface_size(&w, &h, wh_va)) { w = 1280; h = 720; }
    if (w <= 64 || h <= 64) { g_in_frame = 0; return; }

    /* смена EGL-контекста (ротация, пауза/resume) — пересоздаём GLES-ресурсы */
    if (g_uigl_ready && cur_ctx != g_last_egl_ctx) {
        LOGI("ui: смена EGLContext (%p -> %p) — ресоз GL-ресурсов",
             g_last_egl_ctx, cur_ctx);
        br_uigl_shutdown();
        g_uigl_ready = 0;
    }

    if (!g_ctx_ready) br_ui_init_ex(w, h);
    if (!g_uigl_ready) {
        if (br_uigl_init(w, h)) {
            g_uigl_ready = true;
            g_last_egl_ctx = cur_ctx;
            LOGI("ui: GL-бэкенд готов %dx%d (первый кадр)", w, h);
        } else {
            static int ui_init_fail = 0;
            if (++ui_init_fail <= 3) LOGE("ui: br_uigl_init не удался (попытка %d)", ui_init_fail);
            g_in_frame = 0;
            return;
        }
    }

    double now = br_now_ms();
    double dt = g_last_frame_ms ? (now - g_last_frame_ms) : 16.0;
    g_last_frame_ms = now;
    float s = ui_scale_now(h);

    consume_input();

    ImGuiIO *io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)w, (float)h);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ImGui::GetStyle().ScaleAllSizes(s);
    ImGui::GetStyle().Alpha = g_cfg.ui_opacity;

    br_uigl_new_frame();   /* DO перед ImGui::NewFrame() */
    ImGui::NewFrame();

    /* фоновые задачи на GL-треде */
    br_feat_turbo_tick(dt);
    br_feat_drain();

    draw_pill(w, h, s);
    if (g_cfg.show_mini_hud) draw_mini_hud(s);
    if (g_menu_open) draw_menu(w, h, s);
    draw_radar(s);

    /* хит-рект активных окон (для swallow кликов) */
    {
        ImGuiContext *ctx = ImGui::GetCurrentContext();
        g_win_rect_cnt = 0;
        for (int i = 0; ctx && i < ctx->Windows.Size && g_win_rect_cnt < UI_RECT_MAX; i++) {
            ImGuiWindow *wnd = ctx->Windows[i];
            if (!wnd || !wnd->WasActive || wnd->Hidden) continue;
            if (wnd->Flags & ImGuiWindowFlags_NoInputs) continue;
            g_win_rects[g_win_rect_cnt][0] = wnd->Pos.x;
            g_win_rects[g_win_rect_cnt][1] = wnd->Pos.y;
            g_win_rects[g_win_rect_cnt][2] = wnd->Pos.x + wnd->Size.x;
            g_win_rects[g_win_rect_cnt][3] = wnd->Pos.y + wnd->Size.y;
            g_win_rect_cnt++;
        }
    }

    br_uigl_render(w, h);
    g_in_frame = 0;

    static int frame_n = 0;
    if (++frame_n == 1) LOGI("ui: кадр 1 отрисован (%dx%d)", w, h);
    else if (frame_n % 300 == 0) LOGI("ui: кадров всего %d", frame_n);
}

/* ---------------- API для хука ввода (16.111) ---------------- */
bool br_ui_menu_hit(float x, float y)
{
    for (int i = 0; i < g_win_rect_cnt; i++) {
        if (x >= g_win_rects[i][0] && x <= g_win_rects[i][2] &&
            y >= g_win_rects[i][1] && y <= g_win_rects[i][3])
            return true;
    }
    return false;
}

void br_ui_get_btn(float *out_x, float *out_y)
{
    if (out_x) *out_x = g_cfg.btn_x;
    if (out_y) *out_y = g_cfg.btn_y;
}
