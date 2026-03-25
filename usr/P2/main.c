/*
 * P2/main.c - User Process 2
 * Prints lowercase letters 'a' through 'z' in a loop, then repeats.
 * Loaded at 0x82200000 and runs in System mode under the bare-metal OS.
 */

#include "stdio.h"

void main(void)
{
    char c = 'a';

    while (1) {
        PRINT("----From P2: %c\n", c);
        c = (c == 'z') ? 'a' : (char)(c + 1);

        /* Software delay (~0.5 s worth of busy work) */
        for (volatile int d = 0; d < 500000; d++);
    }
}
