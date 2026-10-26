/*
 * cfg.cpp — настройки
 */
#include "br_common.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

br_cfg_t g_cfg;

static char g_path[256] = "";

void br_cfg_init(void)
{
    g_cfg.build        = 0;
    g_cfg.ui_opacity   = 0.92f;
    g_cfg.menu_scale   = 1.0f;
    g_cfg.radar_range  = 400.0f;
    g_cfg.esp_blip     = 5;
    g_cfg.show_players = true;
    g_cfg.show_vehicles= true;
    g_cfg.show_labels  = true;
    g_cfg.show_north   = true;
    g_cfg.show_mini_hud = true;
    g_cfg.local_id     = -1;
    g_cfg.turbo_on     = false;
    g_cfg.turbo_mult   = 5.0f;
    g_cfg.btn_x = 0.03f;
    g_cfg.btn_y = 0.03f;
    g_cfg.log_level    = 3;
}

static const char *try_path(char *out, size_t n, const char *p)
{
    struct stat st;
    if (stat(p, &st) == 0) {
        snprintf(out, n, "%s", p);
        return out;
    }
    /* пробуем создать директорию */
    char d[256];
    snprintf(d, sizeof(d), "%s", p);
    char *sl = strrchr(d, '/');
    if (sl && sl != d) {
        *sl = 0;
        mkdir(d, 0755);
        if (stat(p, &st) == 0) {
            snprintf(out, n, "%s", p);
            return out;
        }
    }
    return 0;
}

const char *br_cfg_path(void)
{
    if (g_path[0]) return g_path;
    const char *cands[] = {
        "/sdcard/brmenu/config.txt",
        "/data/local/tmp/brmenu/config.txt",
        "/tmp/brmenu/config.txt",
    };
    for (int i = 0; i < 3; i++)
        if (try_path(g_path, sizeof(g_path), cands[i]))
            return g_path;
    snprintf(g_path, sizeof(g_path), "/tmp/brmenu-config.txt");
    return g_path;
}

void br_cfg_load(void)
{
    const char *p = br_cfg_path();
    int fd = open(p, O_RDONLY);
    if (fd < 0) return;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = 0;

    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            const char *k = line;
            const char *v = eq + 1;
            /* обрезать пробелы */
            while (*k == ' ') k++;
            char *kend = eq; while (*(kend-1) == ' ') *--kend = 0;
            while (*v == ' ' || *v == '\t') v++;

            #define GETI(name) if (strcmp(k, #name) == 0) g_cfg.name = atoi(v)
            #define GETF(name) if (strcmp(k, #name) == 0) g_cfg.name = (float)atof(v)
            #define GETB(name) if (strcmp(k, #name) == 0) g_cfg.name = (atoi(v) != 0)
            GETI(build); GETF(ui_opacity); GETF(menu_scale); GETF(radar_range);
            GETI(esp_blip); GETB(show_players); GETB(show_vehicles);
            GETB(show_labels); GETB(show_north); GETB(show_mini_hud); GETI(local_id);
            GETB(turbo_on); GETF(turbo_mult); GETF(btn_x); GETF(btn_y); GETI(log_level);
            #undef GETI
            #undef GETF
            #undef GETB
        }
        line = nl;
    }
    LOGI("cfg: загружено из %s", p);
}

void br_cfg_save(void)
{
    const char *p = br_cfg_path();
    FILE *f = fopen(p, "w");
    if (!f) { LOGE("cfg: не могу открыть %s", p); return; }
    fprintf(f, "# Menu-BR config\n");
    fprintf(f, "build=%d\n", g_cfg.build);
    fprintf(f, "ui_opacity=%.2f\n", g_cfg.ui_opacity);
    fprintf(f, "menu_scale=%.2f\n", g_cfg.menu_scale);
    fprintf(f, "radar_range=%.0f\n", g_cfg.radar_range);
    fprintf(f, "esp_blip=%d\n", g_cfg.esp_blip);
    fprintf(f, "show_players=%d\n", g_cfg.show_players ? 1 : 0);
    fprintf(f, "show_vehicles=%d\n", g_cfg.show_vehicles ? 1 : 0);
    fprintf(f, "show_labels=%d\n", g_cfg.show_labels ? 1 : 0);
    fprintf(f, "show_north=%d\n", g_cfg.show_north ? 1 : 0);
    fprintf(f, "show_mini_hud=%d\n", g_cfg.show_mini_hud ? 1 : 0);
    fprintf(f, "local_id=%d\n", g_cfg.local_id);
    fprintf(f, "turbo_on=%d\n", g_cfg.turbo_on ? 1 : 0);
    fprintf(f, "turbo_mult=%.1f\n", g_cfg.turbo_mult);
    fprintf(f, "btn_x=%.3f\n", g_cfg.btn_x);
    fprintf(f, "btn_y=%.3f\n", g_cfg.btn_y);
    fprintf(f, "log_level=%d\n", g_cfg.log_level);
    fclose(f);
    LOGI("cfg: сохранено в %s", p);
}
