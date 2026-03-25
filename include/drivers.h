#ifndef DRIVERS_H
#define DRIVERS_H

/*
 * Platform-specific driver interface.
 *
 * Each function has exactly one implementation, selected at compile time:
 *   (no flag) → drivers/beagle/am335x_timer.c
 *   -DQEMU    → drivers/qemu/virt_timer.c
 *
 * core/sched.c calls these without any #ifdef, keeping the scheduler
 * completely free of hardware knowledge.
 */

/* Start the periodic ~1 s timer interrupt */
void timer_init(void);

/* Configure the interrupt controller and unmask the timer IRQ.
 * Clears the CPU I-bit as its final step. */
void intc_init(void);

/*
 * Called at the top of timer_irq_handler() to acknowledge the interrupt
 * source and re-arm the timer for the next period.
 *
 * Returns an opaque token:
 *   QEMU  — GICC_IAR value (interrupt ID, needed for EOIR)
 *   BBB   — 0              (INTC EOI already written inside this call)
 */
unsigned int timer_irq_begin(void);

/*
 * Called at the bottom of timer_irq_handler() to signal End-of-Interrupt.
 * token — value returned by timer_irq_begin().
 */
void timer_irq_end(unsigned int token);

#ifndef QEMU
/* Disable AM335x WDT1 (must be called before ~60 s elapses, BBB only) */
void wdt_disable(void);
#endif

#endif /* DRIVERS_H */
