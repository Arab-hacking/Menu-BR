/*
 * log.cpp — логирование: кольцевой буфер (для UI) + файл + logcat
 */
#include "br_common.h"
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_BUF_LINES 256
#define LOG_BUF_LEN   192

typedef struct {
    char    text[LOG_BUF_LEN];
    double  ms;
    char    lvl;
} log_line_t;

static log_line_t g_log[LOG_BUF_LINES];
static int        g_log_head = 0; /* куда писать */
static int        g_log_count = 0;
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static int    g_level = 3;
static char   g_file_path[256];
static FILE *g_file = 0;

void br_log_init(const char *dir)
{
    g_level = g_cfg.log_level;
    snprintf(g_file_path, sizeof(g_file_path), "%s/brmenu.log", dir);
    FILE *f = fopen(g_file_path, "w");
    if (f) {
        g_file = f;
        fprintf(f, "=== Menu-BR log ===\n");
    }
}

void br_log(const char *lvl, const char *fmt, ...)
{
    int lv = (lvl[0] == 'E') ? 1 : (lvl[0] == 'W' ? 2 : 3);
    if (lv > g_level) return;

    char body[LOG_BUF_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    double now = br_wall_ms();
    struct timeval tv;
    gettimeofday(&tv, 0);
    struct tm tmv;
    localtime_r(&tv.tv_sec, &tmv);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(tv.tv_usec / 1000));

    pthread_mutex_lock(&g_log_mtx);
    log_line_t *l = &g_log[g_log_head];
    snprintf(l->text, LOG_BUF_LEN, "[%s][%s] %s", stamp, lvl, body);
    l->ms = now;
    l->lvl = lvl[0];
    g_log_head = (g_log_head + 1) % LOG_BUF_LINES;
    g_log_count = g_log_count < LOG_BUF_LINES ? g_log_count + 1 : LOG_BUF_LINES;
    pthread_mutex_unlock(&g_log_mtx);

    if (g_file) {
        fprintf(g_file, "[%s][%s] %s\n", stamp, lvl, body);
        fflush(g_file);
    }

    ANDROID_LOGI("[%s] %s", lvl, body);
}

/* UI: чтение последних lines строк в out (новые в конце). Возвращает число строк. */
int br_log_read(char *out, int out_len, int lines, char *lvl_out)
{
    if (lines <= 0) lines = 1;
    if (lines > LOG_BUF_LINES) lines = LOG_BUF_LINES;

    pthread_mutex_lock(&g_log_mtx);
    int n = g_log_count < lines ? g_log_count : lines;
    int pos = 0;
    for (int i = 0; i < n; i++) {
        int idx = (g_log_head - n + i + LOG_BUF_LINES * 2) % LOG_BUF_LINES;
        if (lvl_out) lvl_out[i] = g_log[idx].lvl;
        int w = snprintf(out + pos, (size_t)(out_len - pos), "%s%s",
                         g_log[idx].text, (i + 1 < n) ? "\n" : "");
        if (w < 0) { pthread_mutex_unlock(&g_log_mtx); return 0; }
        pos += w;
        if (pos >= out_len - 2) break;
    }
    pthread_mutex_unlock(&g_log_mtx);
    return n;
}
