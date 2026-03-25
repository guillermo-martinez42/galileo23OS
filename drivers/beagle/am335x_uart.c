/*
 * drivers/beagle/am335x_uart.c
 *
 * BeagleBone Black: TI 16550-compatible UART0 @ 0x44E09000
 *
 * UART0 is already initialised to 115200 8N1 by U-Boot before the OS
 * image is loaded, so no hardware configuration is needed at boot time.
 *
 * The low-level I/O functions (uart_putc, uart_puts, PRINT) live in
 * lib/stdio.c and access the UART0 THR/LSR registers directly.
 *
 * This file is the correct location for any future BBB UART work:
 *   - Baud-rate reconfiguration
 *   - FIFO depth / trigger-level tuning
 *   - Hardware flow control (RTS/CTS)
 *   - Receive interrupt setup
 */
#ifndef QEMU

#define UART0_BASE  0x44E09000U
#define UART_THR   (*(volatile unsigned int *)(UART0_BASE + 0x00U))
#define UART_IER   (*(volatile unsigned int *)(UART0_BASE + 0x04U))
#define UART_FCR   (*(volatile unsigned int *)(UART0_BASE + 0x08U))
#define UART_LCR   (*(volatile unsigned int *)(UART0_BASE + 0x0CU))
#define UART_LSR   (*(volatile unsigned int *)(UART0_BASE + 0x14U))

/*
 * am335x_uart_reinit — optional: reconfigure UART0 from scratch.
 *
 * Uncomment and call from main() only if U-Boot is not present (e.g.,
 * direct JTAG load) or if a different baud rate is required.
 *
 * Assumes UART functional clock = 48 MHz.
 * DLL/DLH for 115200: divisor = 48000000 / (16 * 115200) = 26 (0x1A)
 */
#if 0
void am335x_uart_reinit(void)
{
    UART_LCR = 0x83U;           /* DLAB=1, 8N1                    */
    UART_THR = 0x1AU;           /* DLL = 26 (115200 @ 48 MHz)     */
    UART_IER = 0x00U;           /* DLH = 0                        */
    UART_LCR = 0x03U;           /* DLAB=0, 8N1 locked in          */
    UART_FCR = 0x07U;           /* Enable + clear TX/RX FIFOs     */
    UART_IER = 0x00U;           /* All UART interrupts disabled    */
}
#endif

#endif /* !QEMU */
