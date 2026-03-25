/*
 * core/sched.c - Round-robin scheduler and context-switch handler
 *
 * This file is intentionally hardware-agnostic.  All interrupt
 * acknowledge / re-arm / EOI operations are delegated to the platform
 * driver via the timer_irq_begin() / timer_irq_end() interface defined
 * in include/drivers.h.
 *
 * Execution flow:
 *   boot/startup.s _irq_handler  →  timer_irq_handler()  (this file)
 *   main()  →  pcb_init() / timer_init() / intc_init()  →  first context switch
 */

#include "sched.h"
#include "drivers.h"
#include "stdio.h"

/* ---------------------------------------------------------------
 * Global register save areas
 * Declared in sched.h; written by boot/startup.s _irq_handler,
 * read/written by timer_irq_handler() below.
 * --------------------------------------------------------------- */
volatile unsigned int saved_regs[13];   /* R0-R12 snapshot             */
volatile unsigned int saved_lr;         /* LR_irq = interrupted PC + 4 */
volatile unsigned int saved_cpsr;       /* SPSR_irq = interrupted CPSR */

/* ---------------------------------------------------------------
 * Timer IRQ handler  (called from _irq_handler in boot/startup.s)
 *
 * On entry (CPU in IRQ mode, IRQs disabled):
 *   saved_regs[0..12] = R0-R12 of the interrupted process
 *   saved_lr          = LR_irq (interrupted PC + 4)
 *   saved_cpsr        = SPSR_irq (interrupted CPSR)
 * --------------------------------------------------------------- */
void timer_irq_handler(void)
{
    int          i;
    unsigned int sys_sp, sys_lr;
    unsigned int new_sp, new_lr;
    unsigned int token;

    /* 1. Acknowledge interrupt + re-arm timer (hardware, platform-specific) */
    token = timer_irq_begin();

    /* 2. Save current process context --------------------------------- */
    for (i = 0; i < 13; i++)
        pcb[current_process].regs[i] = saved_regs[i];

    /* saved_lr = interrupted PC + 4  →  actual resume PC = saved_lr - 4 */
    pcb[current_process].pc   = saved_lr - 4U;
    pcb[current_process].cpsr = saved_cpsr;

    /*
     * Read System-mode banked SP and LR.
     * We are in IRQ mode; a brief switch to System mode lets us read
     * the process-private SP/LR without disturbing IRQ-mode registers.
     * IRQs remain disabled (bit 7 forced high) throughout the switch.
     */
    asm volatile(
        "mrs r0, cpsr\n"
        "bic r1, r0, #0x1F\n"
        "orr r1, r1, #0x1F\n"      /* System mode (0x1F)   */
        "orr r1, r1, #0x80\n"      /* Keep IRQs disabled   */
        "msr cpsr_c, r1\n"
        "mov %0, sp\n"
        "mov %1, lr\n"
        "msr cpsr_c, r0\n"         /* Back to IRQ mode     */
        : "=r"(sys_sp), "=r"(sys_lr)
        :: "r0", "r1"
    );
    pcb[current_process].sp = sys_sp;
    pcb[current_process].lr = sys_lr;

    /* 3. Round-robin schedule ----------------------------------------- */
    pcb[current_process].state = READY;
    current_process = (current_process == 1) ? 2 : 1;
    pcb[current_process].state = RUNNING;

    /* 4. Load next process context into global arrays ------------------ */
    for (i = 0; i < 13; i++)
        saved_regs[i] = pcb[current_process].regs[i];

    /*
     * IRQ return executes:  subs pc, lr, #4
     * To resume at pcb.pc we need  LR_irq = pcb.pc + 4.
     */
    saved_lr   = pcb[current_process].pc + 4U;
    saved_cpsr = pcb[current_process].cpsr;

    /* Restore System-mode SP and LR for the incoming process */
    new_sp = pcb[current_process].sp;
    new_lr = pcb[current_process].lr;
    asm volatile(
        "mrs r0, cpsr\n"
        "bic r1, r0, #0x1F\n"
        "orr r1, r1, #0x1F\n"      /* System mode          */
        "orr r1, r1, #0x80\n"      /* Keep IRQs disabled   */
        "msr cpsr_c, r1\n"
        "mov sp, %0\n"
        "mov lr, %1\n"
        "msr cpsr_c, r0\n"         /* Back to IRQ mode     */
        :: "r"(new_sp), "r"(new_lr)
        : "r0", "r1"
    );

    /* 5. Signal End-of-Interrupt (hardware, platform-specific) --------- */
    timer_irq_end(token);
}

/* ---------------------------------------------------------------
 * OS main entry point — called from _start in boot/startup.s
 * --------------------------------------------------------------- */
void main(void)
{
    /* 1. Initialise UART (enables PL011 on QEMU; no-op on BBB) */
    uart_init();

#ifndef QEMU
    /* 2. Disable watchdog — must complete before ~60 s (BBB only) */
    wdt_disable();
#endif

#ifdef QEMU
    uart_puts("[OS] Running on QEMU virt (Cortex-A15)\r\n");
#else
    uart_puts("[OS] Running on BeagleBone Black (Cortex-A8)\r\n");
#endif

    /* 3. Initialise PCBs with platform load addresses */
    pcb_init();

    /* 4. Start timer (~1 s periodic interrupt) */
    timer_init();

    /* 5. Configure interrupt controller and enable CPU IRQs */
    intc_init();

    /* 6. Memory barrier before first context switch */
    asm volatile("dsb" ::: "memory");

    /*
     * 7. First context switch: jump directly to P1.
     *
     * We are in System mode.  Loading SP/LR with P1's values and then
     * branching to P1's PC transfers control as if P1 had always been
     * running.  When the first timer IRQ fires, P1 is preempted and the
     * round-robin handler above switches to P2.
     */
    pcb[1].state    = RUNNING;
    current_process = 1;

    asm volatile(
        "mov sp, %0\n"
        "mov lr, %1\n"
        "mov pc, lr\n"
        :: "r"(pcb[1].sp), "r"(pcb[1].pc)
    );

    /* Never reached */
    while (1);
}
