#pragma once
// ============================================================================
// android/log.h — минимальный shim для кросс-компиляции zig cc
// (в sysroot zig нет Android-хедеров).
// Реализация __android_log_print предоставлена android_log_shim.cpp:
// пишет в /sdcard/Android/data/com.br.top/files/brmenu.log и (best-effort)
// форвардит в настоящий logcat через dlopen("liblog.so").
// Поэтому NEEDED liblog.so НЕ требуется.
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT = 1,
    ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG   = 3,
    ANDROID_LOG_INFO    = 4,
    ANDROID_LOG_WARN    = 5,
    ANDROID_LOG_ERROR   = 6,
    ANDROID_LOG_FATAL   = 7,
    ANDROID_LOG_SILENT  = 8
};

int __android_log_print(int prio, const char* tag, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
