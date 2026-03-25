/*
 * P1/main.c - User Process 1
 * Prints digits 0 through 9 in a loop, then repeats.
 * Loaded at 0x82100000 and runs in System mode under the bare-metal OS.
 */

#include "../stdio.h"

void main(void)
{
    int n = 0;

    while (1) {
        PRINT("----From P1: %d\n", n);
        n = (n + 1) % 10;

        /* Software delay (~0.5 s worth of busy work) */
        for (volatile int d = 0; d < 500000; d++);
    }
}
