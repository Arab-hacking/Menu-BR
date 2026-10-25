#pragma once
// ============================================================================
// jni.h — минимальный shim для кросс-компиляции zig cc.
// Используется только main_entry.cpp: JNI_OnLoad(JavaVM*, void*) -> jint.
// ============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t  jint;
typedef int64_t  jlong;
typedef uint8_t  jboolean;
typedef int16_t  jshort;
typedef uint16_t jchar;
typedef float    jfloat;
typedef double   jdouble;

typedef jint    jsize;

struct _jobject;
typedef struct _jobject* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jobject jthrowable;

struct _JNIEnv;
typedef struct _JNIEnv JNIEnv;

struct _JavaVM;
typedef struct _JavaVM JavaVM;

#define JNI_FALSE 0
#define JNI_TRUE  1

#define JNI_VERSION_1_2 0x00010002
#define JNI_VERSION_1_4 0x00010004
#define JNI_VERSION_1_6 0x00010006

#define JNIEXPORT
#define JNICALL

#ifdef __cplusplus
}
#endif
