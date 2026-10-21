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

static void open_log_file(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    setvbuf(f, 0, _IONBF, 0);   /* без буферизации — не терять лог при краше */
    g_file = f;
    snprintf(g_file_path, sizeof(g_file_path), "%s", path);
    fprintf(f, "=== Menu-BR log === pid=%d path=%s\n", getpid(), path);
}

void br_log_init(const char *dir)
{
    g_level = g_cfg.log_level;

    /* основной каталог — priv-app files приложения, затем fallback'и */
    const char *dirs[6] = {
        dir,
        "/data/data/com.br.top/files",
        "/sdcard/brmenu",
        "/data/local/tmp/brmenu",
        "/tmp",
        0,
    };
    for (int i = 0; dirs[i]; i++) {
        mkdir(dirs[i], 0700); /* создаём если нет (priv-каталог существует сам) */
        char path[280];
        snprintf(path, sizeof(path), "%s/brmenu.log", dirs[i]);
        open_log_file(path);
        if (g_file) { ANDROID_LOGI("log: файл %s", path); return; }
    }
    ANDROID_LOGE("log: ни один каталог не доступен");
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

/* ================= crash-дамп ================= */

#include <signal.h>
#include <execinfo.h>

static void crash_dump(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    const char *name = "?";
    switch (sig) {
    case SIGSEGV: name = "SIGSEGV"; break;
    case SIGBUS:  name = "SIGBUS";  break;
    case SIGABRT: name = "SIGABRT"; break;
    case SIGFPE:  name = "SIGFPE";  break;
    case SIGILL:  name = "SIGILL";  break;
    case SIGTRAP: name = "SIGTRAP"; break;
    }

    int fd = (g_file && fileno(g_file) >= 0) ? fileno(g_file) : -1;
    if (fd < 0) fd = open("/dev/stderr", O_WRONLY);
    if (fd < 0) _exit(128 + sig);

    char b[512];
    int n = snprintf(b, sizeof(b),
        "\n===== CRASH %s (si_addr=%p) pid=%d tid=%d =====\n",
        name, si ? si->si_addr : 0, getpid(), (long)gettid());
    if (n > 0) write(fd, b, (size_t)n);

    /* backtrace в fd (async-signal-safe) */
    void *bt[48];
    int bn = backtrace(bt, 48);
    if (bn > 0) {
        n = snprintf(b, sizeof(b), "--- backtrace (%d) ---\n", bn);
        if (n > 0) write(fd, b, (size_t)n);
        backtrace_symbols_fd(bt, bn, fd);
    }

    /* maps: базовые адреса модулей в момент краха */
    int mf = open("/proc/self/maps", O_RDONLY);
    if (mf >= 0) {
        n = snprintf(b, sizeof(b), "--- /proc/self/maps ---\n");
        if (n > 0) write(fd, b, (size_t)n);
        char mb[1024];
        ssize_t r;
        int total = 0;
        while ((r = read(mf, mb, sizeof(mb))) > 0 && total < 65536) {
            write(fd, mb, (size_t)r);
            total += (int)r;
        }
        close(mf);
    }

    /* завершаем дефолтным обработчиком (tombstone как обычно) */
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(128 + sig);
}

void br_crash_init(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGTRAP };
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_dump;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND; /* после дампа — дефолтное поведение */
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, 0);
}

/* ================= heartbeat ================= */

static volatile int g_hb_run = 0;
static pthread_t    g_hb_tid;
static int          g_hb_last_pid_logged = 0;

static void *hb_thread(void *arg)
{
    (void)arg;
    int tick = 0;
    while (g_hb_run) {
        for (int i = 0; i < 20 && g_hb_run; i++) usleep(100000); /* 2 c */
        if (!g_hb_run) break;
        tick++;
        int mytid = (int)gettid();
        if (mytid != g_hb_last_pid_logged) {
            g_hb_last_pid_logged = mytid;
            ANDROID_LOGI("hb: thread started tid=%d", mytid);
        }
        ANDROID_LOGI("hb: alive tick=%d", tick);
    }
    return 0;
}

void br_heartbeat_start(void)
{
    if (g_hb_run) return;
    g_hb_run = 1;
    if (pthread_create(&g_hb_tid, 0, hb_thread, 0) != 0) { g_hb_run = 0; return; }
    pthread_detach(g_hb_tid);
}

void br_heartbeat_stop(void)
{
    g_hb_run = 0;
}
