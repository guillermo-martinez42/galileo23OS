/*
 * drivers/beagle/am335x_timer.c
 *
 * BeagleBone Black (AM335x Cortex-A8) peripheral drivers:
 *   WDT1       — Watchdog Timer 1 (disable to prevent ~60 s reboot)
 *   DMTimer2   — Periodic ~1 s overflow interrupt
 *   TI INTC    — Interrupt Controller (unmask IRQ 68 = DMTimer2)
 *
 * This file is compiled only for the BBB target (no -DQEMU flag).
 * The #ifndef guard makes it safe if the file is accidentally included
 * in a QEMU build.
 */
#ifndef QEMU

/* ---------------------------------------------------------------
 * Watchdog Timer 1 registers  (base 0x44E35000)
 * --------------------------------------------------------------- */
#define WDT1_BASE    0x44E35000U
#define WDT_WWPS (*(volatile unsigned int *)(WDT1_BASE + 0x34U)) /* Write-posting status */
#define WDT_WSPR (*(volatile unsigned int *)(WDT1_BASE + 0x48U)) /* Start/stop sequence  */

/* ---------------------------------------------------------------
 * DMTimer2 registers  (base 0x48040000)
 * --------------------------------------------------------------- */
#define TIMER2_BASE  0x48040000U
#define TIMER_TISR (*(volatile unsigned int *)(TIMER2_BASE + 0x28U)) /* IRQ status          */
#define TIMER_TIER (*(volatile unsigned int *)(TIMER2_BASE + 0x2CU)) /* IRQ enable          */
#define TIMER_TCLR (*(volatile unsigned int *)(TIMER2_BASE + 0x38U)) /* Control             */
#define TIMER_TCRR (*(volatile unsigned int *)(TIMER2_BASE + 0x3CU)) /* Counter value       */
#define TIMER_TLDR (*(volatile unsigned int *)(TIMER2_BASE + 0x40U)) /* Auto-reload value   */

/* ---------------------------------------------------------------
 * TI Interrupt Controller registers  (base 0x48200000)
 * --------------------------------------------------------------- */
#define INTC_BASE        0x48200000U
#define INTC_SYSCONFIG  (*(volatile unsigned int *)(INTC_BASE + 0x10U)) /* Soft reset        */
#define INTC_SYSSTATUS  (*(volatile unsigned int *)(INTC_BASE + 0x14U)) /* Reset done flag   */
#define INTC_SIR_IRQ    (*(volatile unsigned int *)(INTC_BASE + 0x40U)) /* Active IRQ number */
#define INTC_CONTROL    (*(volatile unsigned int *)(INTC_BASE + 0x48U)) /* New IRQ / FIQ EOI */
#define INTC_MIR_CLEAR2 (*(volatile unsigned int *)(INTC_BASE + 0xC8U)) /* Unmask bank 2     */

/* ---------------------------------------------------------------
 * wdt_disable — stop the watchdog before it resets the board
 *
 * The WDT is locked by hardware; the magic write sequence
 * 0xAAAA → 0x5555 unlocks and disables it.  Each write must
 * complete (W_PEND_WSPR clear in WWPS) before the next.
 * --------------------------------------------------------------- */
void wdt_disable(void)
{
    WDT_WSPR = 0xAAAAU;
    while (WDT_WWPS & (1U << 4));   /* Wait W_PEND_WSPR clear */
    WDT_WSPR = 0x5555U;
    while (WDT_WWPS & (1U << 4));
}

/* ---------------------------------------------------------------
 * timer_init — configure DMTimer2 for ~1 s overflow interrupts
 *
 * Input clock: 24 MHz (M_OSC after DPLL bypass)
 * TLDR = 0xFE800000 → 0x01800000 ticks to overflow ≈ 1.05 s
 * TCLR bits: ST=1 (start), AR=1 (auto-reload)
 * --------------------------------------------------------------- */
void timer_init(void)
{
    TIMER_TCLR = 0U;                /* Stop timer                     */
    TIMER_TLDR = 0xFE800000U;       /* Auto-reload value (~1 s)       */
    TIMER_TCRR = 0xFE800000U;       /* Pre-load counter to same value */
    TIMER_TISR = 0x7U;              /* Clear all pending status flags */
    TIMER_TIER = 0x2U;              /* Enable overflow interrupt (OVF)*/
    TIMER_TCLR = 0x3U;              /* Start + auto-reload            */
}

/* ---------------------------------------------------------------
 * intc_init — reset TI INTC and unmask DMTimer2 (IRQ 68)
 *
 * IRQ 68 falls in bank 2 (IRQs 64–95): bit = 68 − 64 = 4.
 * Finally, clears the I-bit in CPSR to allow IRQs to reach the CPU.
 * --------------------------------------------------------------- */
void intc_init(void)
{
    /* Soft-reset INTC and wait for completion */
    INTC_SYSCONFIG = 0x2U;
    while (!(INTC_SYSSTATUS & 0x1U));

    /* Unmask IRQ 68 (DMTimer2 overflow) in bank 2 */
    INTC_MIR_CLEAR2 = (1U << 4);

    /* Enable IRQs in CPU: clear I-bit in CPSR */
    asm volatile(
        "mrs r0, cpsr\n"
        "bic r0, r0, #0x80\n"
        "msr cpsr_c, r0\n"
        ::: "r0"
    );
}

/* ---------------------------------------------------------------
 * timer_irq_begin — acknowledge DMTimer2 interrupt, signal INTC EOI
 *
 * Called at the top of timer_irq_handler() before any scheduling.
 * Returns 0 (BBB does not need a token for EOI).
 * --------------------------------------------------------------- */
unsigned int timer_irq_begin(void)
{
    TIMER_TISR   = 0x2U;    /* Clear overflow (OVF) status flag */
    INTC_CONTROL = 0x1U;    /* Signal new-IRQ EOI to INTC       */
    return 0U;
}

/* ---------------------------------------------------------------
 * timer_irq_end — nothing to do on BBB (EOI sent in timer_irq_begin)
 * --------------------------------------------------------------- */
void timer_irq_end(unsigned int token)
{
    (void)token;
}

#endif /* !QEMU */
