#ifndef SEVENTHOS_KSTRING_H
#define SEVENTHOS_KSTRING_H

#include <stddef.h>

void *memset(void *dst, int value, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *s);

#endif