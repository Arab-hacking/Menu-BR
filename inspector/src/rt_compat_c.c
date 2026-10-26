/*
 * rt_compat_c.c — sized operator new/delete (mangled) как C-символы.
 * В C++-TU libc++ <new> объявляет их с visibility("default"), что
 * конфликтует с -fvisibility=hidden; отсюда экспортируются hidden.
 */
#include <stdlib.h>

/* glibc-механизм errno (макро errno в glibc-хедерах -> __errno_location).
   bionic-errno — другой механизм; здесь только удовлетворить ссылки из
   кода, скомпилированного с glibc-хедерами. hidden: не экспорт. */
static __thread int br_errno_tl = 0;
__attribute__((visibility("hidden"))) int* __errno_location(void) { return &br_errno_tl; }

void _ZdlPvm(void* p, long size) { (void)size; free(p); }
void _ZdaPvm(void* p, long size) { (void)size; free(p); }
