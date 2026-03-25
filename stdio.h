#ifndef STDIO_H
#define STDIO_H

/* UART initialisation (no-op on BBB, enables PL011 on QEMU) */
void uart_init(void);

/* Low-level UART output */
void uart_putc(char c);
void uart_puts(const char *s);

/* Formatted print — supports %d %c %s %x %% */
void PRINT(const char *fmt, ...);

#endif /* STDIO_H */
