/*
 * usr/P1/main.c — User Process 1 (Phase 2)
 *
 * Runs in USR mode; the only legal way to touch hardware (UART) is
 * sys_write through the SVC trap.  Prints digits 0-9 in a loop,
 * yielding cooperatively after each line so the round-robin scheduler
 * has frequent opportunities to swap us out (timer IRQ also preempts).
 *
 * No kernel PRINT, no direct UART register access — clean user/kernel
 * break per Phase 2 §3.
 */

#include "user_syscalls.h"

static const char prefix[] = "----From P1: ";    /* 13 chars, no NUL */

int main(void)
{
    /* "----From P1: D\n"  →  15 bytes */
    char buf[16];
    int  i;
    int  n = 0;

    for (i = 0; i < 13; i++) buf[i] = prefix[i];
    buf[14] = '\n';

    while (1) {
        buf[13] = (char)('0' + n);
        sys_write(1, buf, 15);

        n = (n + 1) % 10;

        /* Short busy delay so output is human-readable, then yield. */
        for (volatile int d = 0; d < 500000; d++) { }
        sys_yield();
    }

    /* Unreachable — left in for shape; would exit cleanly if reached. */
    sys_exit(0);
}
