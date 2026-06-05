/*
 * usr/P2/main.c — User Process 2 (Phase 2)
 *
 * Runs in USR mode; uses only sys_write for output.  Prints letters
 * a-z in a loop with cooperative yields between lines.
 */

#include "user_syscalls.h"

static const char prefix[] = "----From P2: ";    /* 13 chars, no NUL */

int main(void)
{
    /* "----From P2: L\n"  →  17 bytes */
    char buf[18];
    int  i;
    char c = 'a';

    for (i = 0; i < 13; i++) buf[i] = prefix[i];
    buf[17] = '\n';

    while (1) {
        buf[13] = c;
        sys_write(1, buf, 18);

        c = (c == 'z') ? 'a' : (char)(c + 1);

        for (volatile int d = 0; d < 500000; d++) { }
        sys_yield();
    }

    sys_exit(0);
}
