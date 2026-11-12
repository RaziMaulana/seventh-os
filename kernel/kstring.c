#include <stddef.h>
#include "kstring.h"

void *memset(void *dst, int value, size_t count)
{
    void *ret = dst;
    __asm__ volatile("rep stosb"
                     : "+D"(dst), "+c"(count)
                     : "a"(value)
                     : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t count)
{
    void *ret = dst;
    __asm__ volatile("rep movsb"
                     : "+D"(dst), "+S"(src), "+c"(count)
                     :
                     : "memory");
    return ret;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < count; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}