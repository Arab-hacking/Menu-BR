/*
 * jnistub.h — минимальные JNI-типы для перекрёстной сборки без NDK
 * (на Android используются настоящие заголовки)
 */
#ifndef BR_JNISTUB_H
#define BR_JNISTUB_H

#if defined(__ANDROID__)
#include <jni.h>
#else
typedef int            jint;
typedef unsigned int   juint;
typedef signed char    jbyte;
typedef short          jshort;
typedef unsigned short jushort;
typedef long           jlong;
typedef unsigned long  julong;
typedef float          jfloat;
typedef double         jdouble;
struct _JavaVM;
struct _JNIEnv;
typedef struct _JavaVM JavaVM;
typedef struct _JNIEnv JNIEnv;
#endif

#endif
