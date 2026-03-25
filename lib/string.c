/*
 * string.c - Minimal string/memory utilities for bare-metal AM335x
 */

#include "string.h"

unsigned int strlen(const char *s)
{
    unsigned int len = 0U;
    while (*s++) len++;
    return len;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

void *memset(void *ptr, int value, unsigned int size)
{
    unsigned char *p = (unsigned char *)ptr;
    while (size--)
        *p++ = (unsigned char)value;
    return ptr;
}

void *memcpy(void *dst, const void *src, unsigned int size)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (size--)
        *d++ = *s++;
    return dst;
}
