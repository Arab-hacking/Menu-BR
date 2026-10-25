/*
 * rt_compat_c.c — sized operator new/delete (mangled) как C-символы.
 * В C++-TU libc++ <new> объявляет их с visibility("default"), что
 * конфликтует с -fvisibility=hidden; отсюда экспортируются hidden.
 */
#include <stdlib.h>

void _ZdlPvm(void* p, long size) { (void)size; free(p); }
void _ZdaPvm(void* p, long size) { (void)size; free(p); }
