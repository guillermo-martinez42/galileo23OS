#ifndef STRING_H
#define STRING_H

unsigned int  strlen (const char *s);
char         *strcpy (char *dst, const char *src);
void         *memset (void *ptr, int value, unsigned int size);
void         *memcpy (void *dst, const void *src, unsigned int size);

#endif /* STRING_H */
