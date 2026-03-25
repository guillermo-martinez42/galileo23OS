/*
 * stdio.c - Minimal UART I/O and formatted PRINT for bare-metal ARM
 *
 * Target is selected at compile time via -D flag:
 *   -DQEMU  : ARM PL011 UART @ 0x09000000 (QEMU -M virt)
 *   (none)  : TI 16550-compatible UART @ 0x44E09000 (BeagleBone Black AM335x)
 *
 * Compiled into every binary (OS, P1, P2) so each has its own copy.
 */

#include "stdio.h"
#include <stdarg.h>

/* ---------------------------------------------------------------
 * UART register definitions — platform-selected
 * --------------------------------------------------------------- */
#ifdef QEMU

/* ARM PL011 UART @ 0x09000000 (QEMU -M virt) */
#define UART_BASE    0x09000000U
#define UART_DR      (*(volatile unsigned int *)(UART_BASE + 0x00U)) /* Data Register        */
#define UART_FR      (*(volatile unsigned int *)(UART_BASE + 0x18U)) /* Flag Register        */
#define UART_IBRD    (*(volatile unsigned int *)(UART_BASE + 0x24U)) /* Integer baud divisor */
#define UART_FBRD    (*(volatile unsigned int *)(UART_BASE + 0x28U)) /* Fractional baud div  */
#define UART_LCR_H   (*(volatile unsigned int *)(UART_BASE + 0x2CU)) /* Line control         */
#define UART_CR      (*(volatile unsigned int *)(UART_BASE + 0x30U)) /* Control register     */
#define UART_FR_TXFF (1U << 5)  /* TX FIFO full  */

#else /* BeagleBone Black — TI 16550-compatible UART0 @ 0x44E09000 */

#define UART0_BASE  0x44E09000U
#define UART_THR    (*(volatile unsigned int *)(UART0_BASE + 0x00U)) /* Transmit Holding Reg */
#define UART_LSR    (*(volatile unsigned int *)(UART0_BASE + 0x14U)) /* Line Status Register */
#define UART_THRE   (1U << 5)  /* TX Holding Register Empty */

#endif /* QEMU */

/* ---------------------------------------------------------------
 * UART initialisation
 *   QEMU  : configure PL011 (QEMU reset leaves UARTEN=0)
 *   BBB   : U-Boot already initialised UART0 at 115200 8N1 — no-op
 * --------------------------------------------------------------- */
void uart_init(void)
{
#ifdef QEMU
    UART_CR    = 0U;                        /* Disable UART before config   */
    UART_IBRD  = 1U;                        /* Divisors: QEMU ignores them, */
    UART_FBRD  = 0U;                        /*   but must be non-zero       */
    UART_LCR_H = (3U << 5) | (1U << 4);    /* 8-bit word, FIFO enable      */
    UART_CR    = (1U << 0) | (1U << 8) | (1U << 9); /* UARTEN | TXE | RXE */
#endif
}

/* ---------------------------------------------------------------
 * Low-level character output
 * --------------------------------------------------------------- */
void uart_putc(char c)
{
#ifdef QEMU
    while (UART_FR & UART_FR_TXFF);         /* Spin while TX FIFO is full  */
    UART_DR  = (unsigned int)(unsigned char)c;
#else
    while (!(UART_LSR & UART_THRE));        /* Spin until THR empty        */
    UART_THR = (unsigned int)(unsigned char)c;
#endif
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/* ---------------------------------------------------------------
 * Conversion helpers
 * --------------------------------------------------------------- */
static void print_uint(unsigned int n)
{
    char buf[12];
    int  i = 0;

    if (n == 0U) { uart_putc('0'); return; }

    while (n > 0U) {
        buf[i++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    /* Digits are reversed — emit last-in first-out */
    while (i > 0)
        uart_putc(buf[--i]);
}

static void print_int(int n)
{
    if (n < 0) {
        uart_putc('-');
        print_uint((unsigned int)(-n));
    } else {
        print_uint((unsigned int)n);
    }
}

static void print_hex(unsigned int n)
{
    char buf[9];
    int  i = 0;

    if (n == 0U) { uart_putc('0'); return; }

    while (n > 0U) {
        int d = (int)(n & 0xFU);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        n >>= 4;
    }
    while (i > 0)
        uart_putc(buf[--i]);
}

/* ---------------------------------------------------------------
 * PRINT — formatted output (subset of printf)
 * Supported: %d  %c  %s  %x  %%
 * \n is translated to \r\n for serial consoles.
 * --------------------------------------------------------------- */
void PRINT(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case 'd':
                print_int(va_arg(args, int));
                break;
            case 'c':
                uart_putc((char)va_arg(args, int));
                break;
            case 's':
                uart_puts(va_arg(args, const char *));
                break;
            case 'x':
                print_hex(va_arg(args, unsigned int));
                break;
            case '%':
                uart_putc('%');
                break;
            default:
                uart_putc('%');
                uart_putc(*fmt);
                break;
            }
        } else if (*fmt == '\n') {
            uart_putc('\r');
            uart_putc('\n');
        } else {
            uart_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
