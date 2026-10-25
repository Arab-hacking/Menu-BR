// ============================================================================
// android_log_shim.cpp — реализация __android_log_print без NEEDED liblog.so.
//
// Записывает в /sdcard/Android/data/com.br.top/files/brmenu.log
// (путь, указанный пользователем; внешняя папка Android/data/<pkg>/files).
// Дополнительно best-effort форвардит в настоящий logcat через
// dlopen("liblog.so") — если библиотека доступна в namespace приложения,
// строки дублируются и в logcat (тэг сохранён).
//
// Сильный символ __android_log_print в этом .so перехватывает ВСЕ вызовы
// из нашего кода (и их BR_*_LOGI макросы).
// ============================================================================

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__ANDROID__)
#define BR_LOG_FILE_PATH "/sdcard/Android/data/com.br.top/files/brmenu.log"
#define BR_LOG_FILE_OLD  "/sdcard/Android/data/com.br.top/files/brmenu.log.old"
#else
#define BR_LOG_FILE_PATH "/tmp/brmenu.log"
#endif

namespace {

struct LogShim {
    void*   logcat_fn = nullptr; // int (*)(int, const char*, const char*, ...)
    bool    logcat_tried = false;
    int     fd = -1;
    bool    fd_tried = false;
    size_t  size_hint = 0;

    LogShim() {
        // Best-effort: настоящий logcat (liblog.so) — если виден в namespace
        void* lib = dlopen("liblog.so", RTLD_NOW | RTLD_NOLOAD);
        if (!lib) {
            lib = dlopen("liblog.so", RTLD_NOW);
        }
        if (lib) {
            logcat_fn = dlsym(lib, "__android_log_print");
        }
        logcat_tried = true;
    }

    void WriteFile(const char* line, size_t len) {
        if (!fd_tried) {
            fd_tried = true;
            // Best-effort: папка внешних файлов приложения (обычно уже есть)
            (void)mkdir("/sdcard/Android/data/com.br.top", 0755);
            (void)mkdir("/sdcard/Android/data/com.br.top/files", 0755);
            fd = open(BR_LOG_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                struct stat st{};
                if (fstat(fd, &st) == 0) {
                    size_hint = static_cast<size_t>(st.st_size);
                }
            }
        }
        if (fd < 0) {
            return;
        }

        // Простое вращение: > 1 МБ -> .log.old
        if (size_hint > (1u << 20)) {
            close(fd);
            fd = -1;
            rename(BR_LOG_FILE_PATH, BR_LOG_FILE_OLD);
            fd = open(BR_LOG_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
            size_hint = 0;
            if (fd < 0) {
                return;
            }
        }

        const ssize_t w = write(fd, line, len);
        if (w > 0) {
            size_hint += static_cast<size_t>(w);
        }
    }
};

LogShim& Shim() {
    static LogShim s;
    return s;
}

char LevelChar(int prio) {
    switch (prio) {
        case 2: return 'V';
        case 3: return 'D';
        case 4: return 'I';
        case 5: return 'W';
        case 6: return 'E';
        case 7: return 'F';
        default: return '?';
    }
}

} // namespace

extern "C" int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    // INFO и выше — в файл (VERBOSE/DEBUG слишком шумные)
    if (prio < 4) {
        prio = 4;
    }

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    const int msg_len = vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);
    if (msg_len < 0) {
        msg[0] = '\0';
    }

    // 1) настоящий logcat (если liblog доступен) — тэг оригинальный
    LogShim& shim = Shim();
    if (shim.logcat_fn) {
        typedef int (*LogPrintFn)(int, const char*, const char*, ...);
        static LogPrintFn fn = reinterpret_cast<LogPrintFn>(shim.logcat_fn);
        fn(prio, tag ? tag : "?", "%s", msg);
    }

    // 2) файл brmenu.log
    char line[1400];
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    const time_t t = tv.tv_sec;
    struct tm tmv{};
    localtime_r(&t, &tmv);

    char ts[32];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, static_cast<int>(tv.tv_usec / 1000));

    const int len = snprintf(line, sizeof(line),
                             "%s [%c/%s] %s\n", ts, LevelChar(prio), tag ? tag : "?", msg);
    if (len > 0) {
        shim.WriteFile(line, static_cast<size_t>(len));
    }
    return msg_len;
}
