#pragma once
// xlocale.h — минимальный shim для android-ветки libc++ (<vector>/<locale>).
// Под zig sysroot (glibc) locale_t уже объявлен в <locale.h> — подключаем его.
// В чисто Android-окружении (NDK) настоящий xlocale.h всё равно даёт locale_t.
#ifdef __GLIBC__
#include <locale.h>
#else
#ifndef _XLOCALE_H_SHIM
#define _XLOCALE_H_SHIM
typedef void* locale_t;
#define LC_CTYPE    0
#define LC_NUMERIC  1
#define LC_TIME     2
#define LC_COLLATE  3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL      6
#endif
#endif
