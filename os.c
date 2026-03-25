/*
 * os.c - Bare-Metal Multiprogramming OS
 *
 * Supports two targets selected at compile time via -D:
 *   (none) : BeagleBone Black — AM335x Cortex-A8
 *              WDT1 disable, DMTimer2, TI INTC
 *   -DQEMU : QEMU -M virt — Cortex-A15
 *              No WDT, ARM Generic Timer (CP15), ARM GICv2
 *
 * Responsibilities:
 *   - Disable watchdog (BBB only)
 *   - Initialize timer (~1 s periodic interrupt)
 *   - Initialize interrupt controller
 *   - Manage Process Control Blocks (PCBs)
 *   - Context-switch logic (timer_irq_handler)
 *   - First context switch to P1
 */

#include "os.h"
#include "stdio.h"
#include "string.h"

/* ===============================================================
 * Hardware registers — BeagleBone Black (AM335x)
 * =============================================================== */
#ifndef QEMU

/* Watchdog Timer 1 */
#define WDT1_BASE        0x44E35000U
#define WDT_WWPS    (*(volatile unsigned int *)(WDT1_BASE + 0x34U))
#define WDT_WSPR    (*(volatile unsigned int *)(WDT1_BASE + 0x48U))

/* DMTimer2 */
#define TIMER2_BASE      0x48040000U
#define TIMER_TISR  (*(volatile unsigned int *)(TIMER2_BASE + 0x28U))
#define TIMER_TIER  (*(volatile unsigned int *)(TIMER2_BASE + 0x2CU))
#define TIMER_TCLR  (*(volatile unsigned int *)(TIMER2_BASE + 0x38U))
#define TIMER_TCRR  (*(volatile unsigned int *)(TIMER2_BASE + 0x3CU))
#define TIMER_TLDR  (*(volatile unsigned int *)(TIMER2_BASE + 0x40U))

/* TI Interrupt Controller (INTC) */
#define INTC_BASE        0x48200000U
#define INTC_SYSCONFIG  (*(volatile unsigned int *)(INTC_BASE + 0x10U))
#define INTC_SYSSTATUS  (*(volatile unsigned int *)(INTC_BASE + 0x14U))
#define INTC_SIR_IRQ    (*(volatile unsigned int *)(INTC_BASE + 0x40U))
#define INTC_CONTROL    (*(volatile unsigned int *)(INTC_BASE + 0x48U))
#define INTC_MIR_CLEAR2 (*(volatile unsigned int *)(INTC_BASE + 0xC8U))

/* ===============================================================
 * Hardware registers — QEMU -M virt (ARM GICv2 + Generic Timer)
 * =============================================================== */
#else /* QEMU */

/*
 * ARM GICv2
 *   Distributor base : 0x08000000
 *   CPU interface    : 0x08010000
 */
#define GICD_BASE        0x08000000U
#define GICC_BASE        0x08010000U

/* GIC Distributor registers */
#define GICD_CTLR      (*(volatile unsigned int *)(GICD_BASE + 0x000U)) /* Enable distributor   */
#define GICD_ISENABLER0 (*(volatile unsigned int *)(GICD_BASE + 0x100U)) /* Enable SGI/PPI 0-31 */

/* GIC CPU Interface registers */
#define GICC_CTLR  (*(volatile unsigned int *)(GICC_BASE + 0x000U)) /* Enable CPU interface */
#define GICC_PMR   (*(volatile unsigned int *)(GICC_BASE + 0x004U)) /* Priority mask        */
#define GICC_IAR   (*(volatile unsigned int *)(GICC_BASE + 0x00CU)) /* Interrupt Acknowledge*/
#define GICC_EOIR  (*(volatile unsigned int *)(GICC_BASE + 0x010U)) /* End of Interrupt     */

/*
 * ARM Generic Timer — Physical Non-Secure Timer
 *   INTID 30 = PPI 14 (GICD_ISENABLER0 bit 30)
 *
 * CP15 registers (Cortex-A15):
 *   CNTFRQ   : c14, c0, 0  — counter frequency (Hz)
 *   CNTP_TVAL: c14, c2, 0  — physical countdown value
 *   CNTP_CTL : c14, c2, 1  — physical timer control
 *                            bit0=ENABLE, bit1=IMASK, bit2=ISTATUS(ro)
 */
#define GTIMER_INTID  30U

/* Frequency read at timer_init() time; shared with timer_irq_handler() */
static unsigned int timer_freq;

#endif /* QEMU */

/* ---------------------------------------------------------------
 * Globals shared with root.s IRQ handler
 * --------------------------------------------------------------- */
volatile unsigned int saved_regs[13];   /* R0-R12 snapshot              */
volatile unsigned int saved_lr;         /* LR_irq = interrupted PC + 4  */
volatile unsigned int saved_cpsr;       /* SPSR_irq = interrupted CPSR  */

/* ---------------------------------------------------------------
 * Scheduler state
 * --------------------------------------------------------------- */
pcb_t pcb[3];          /* pcb[0]=OS (unused), pcb[1]=P1, pcb[2]=P2 */
int   current_process = 1;

/* ---------------------------------------------------------------
 * Watchdog disable (BeagleBone only)
 * Must be the very first thing called in main() or the board
 * resets after ~60 seconds.
 * --------------------------------------------------------------- */
#ifndef QEMU
static void wdt_disable(void)
{
    WDT_WSPR = 0xAAAAU;
    while (WDT_WWPS & (1U << 4));   /* Wait W_PEND_WSPR clear */
    WDT_WSPR = 0x5555U;
    while (WDT_WWPS & (1U << 4));
}
#endif

/* ---------------------------------------------------------------
 * Timer initialisation
 *
 * BBB  : DMTimer2 @ 24 MHz, ~1 s overflow interrupt
 *         TLDR = 0xFE800000 → ~1.05 s at 24 MHz
 *
 * QEMU : ARM Generic Timer (CP15)
 *         Read CNTFRQ, load CNTP_TVAL with that value → 1 s countdown
 *         Enable CNTP_CTL (ENABLE=1, IMASK=0)
 * --------------------------------------------------------------- */
static void timer_init(void)
{
#ifdef QEMU
    /* Read system counter frequency from CNTFRQ */
    asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(timer_freq));

    /* Load Physical Timer countdown for exactly 1 second */
    asm volatile("mcr p15, 0, %0, c14, c2, 0" :: "r"(timer_freq));
    asm volatile("isb");

    /* Enable timer, interrupt not masked (ENABLE=1, IMASK=0) */
    asm volatile("mcr p15, 0, %0, c14, c2, 1" :: "r"(0x1U));
    asm volatile("isb");
#else
    TIMER_TCLR = 0U;                /* Stop timer                      */
    TIMER_TLDR = 0xFE800000U;       /* Reload value (~1 s at 24 MHz)   */
    TIMER_TCRR = 0xFE800000U;       /* Pre-load counter                */
    TIMER_TISR = 0x7U;              /* Clear all pending status flags  */
    TIMER_TIER = 0x2U;              /* Enable overflow interrupt (OVF) */
    TIMER_TCLR = 0x3U;              /* Start + auto-reload             */
#endif
}

/* ---------------------------------------------------------------
 * Interrupt controller initialisation
 *
 * BBB  : TI INTC — soft reset, unmask DMTimer2 IRQ (IRQ 68, bank 2, bit 4)
 *
 * QEMU : ARM GICv2
 *         Enable distributor (GICD_CTLR)
 *         Enable Generic Timer PPI (INTID 30, bit 30 of GICD_ISENABLER0)
 *         Set priority mask to 0xFF (accept all priorities)
 *         Enable CPU interface (GICC_CTLR)
 *
 * Both: clear I-bit in CPSR to start accepting IRQs.
 * --------------------------------------------------------------- */
static void intc_init(void)
{
#ifdef QEMU
    /* Enable GIC Distributor */
    GICD_CTLR = 1U;

    /* Enable Generic Timer interrupt (INTID 30 = PPI 14, bit 30) */
    GICD_ISENABLER0 = (1U << GTIMER_INTID);

    /* Accept all interrupt priorities */
    GICC_PMR = 0xFFU;

    /* Enable GIC CPU Interface */
    GICC_CTLR = 1U;
#else
    /* Soft-reset INTC */
    INTC_SYSCONFIG = 0x2U;
    while (!(INTC_SYSSTATUS & 0x1U));  /* Wait reset complete */

    /* Unmask IRQ 68: bank 2 (IRQs 64-95), bit = 68-64 = 4 */
    INTC_MIR_CLEAR2 = (1U << 4);
#endif

    /* Enable IRQs in CPU: clear I-bit in CPSR */
    asm volatile(
        "mrs r0, cpsr\n"
        "bic r0, r0, #0x80\n"
        "msr cpsr_c, r0\n"
        ::: "r0"
    );
}

/* ---------------------------------------------------------------
 * PCB initialisation
 *
 * Process addresses differ per platform:
 *   BBB  : P1 @ 0x82100000, P2 @ 0x82200000  (loaded by U-Boot)
 *   QEMU : P1 @ 0x40100000, P2 @ 0x40200000  (loaded via -device loader)
 * --------------------------------------------------------------- */
static void pcb_init(void)
{
    int i;

#ifdef QEMU
    /* P1: prints digits 0-9 */
    pcb[1].pid   = 1;
    pcb[1].pc    = 0x40100000U;   /* P1 entry point  (p1_qemu.ld)  */
    pcb[1].sp    = 0x40112000U;   /* Top of P1 stack               */
    pcb[1].lr    = 0x40100000U;
    pcb[1].cpsr  = 0x1FU;         /* System mode, IRQs on          */
    pcb[1].state = READY;
    for (i = 0; i < 13; i++) pcb[1].regs[i] = 0U;

    /* P2: prints letters a-z */
    pcb[2].pid   = 2;
    pcb[2].pc    = 0x40200000U;   /* P2 entry point  (p2_qemu.ld)  */
    pcb[2].sp    = 0x40212000U;   /* Top of P2 stack               */
    pcb[2].lr    = 0x40200000U;
    pcb[2].cpsr  = 0x1FU;
    pcb[2].state = READY;
    for (i = 0; i < 13; i++) pcb[2].regs[i] = 0U;
#else
    /* P1: prints digits 0-9 */
    pcb[1].pid   = 1;
    pcb[1].pc    = 0x82100000U;   /* P1 entry point  (p1.ld)       */
    pcb[1].sp    = 0x82112000U;   /* Top of P1 stack               */
    pcb[1].lr    = 0x82100000U;
    pcb[1].cpsr  = 0x1FU;         /* System mode, IRQs on          */
    pcb[1].state = READY;
    for (i = 0; i < 13; i++) pcb[1].regs[i] = 0U;

    /* P2: prints letters a-z */
    pcb[2].pid   = 2;
    pcb[2].pc    = 0x82200000U;   /* P2 entry point  (p2.ld)       */
    pcb[2].sp    = 0x82212000U;   /* Top of P2 stack               */
    pcb[2].lr    = 0x82200000U;
    pcb[2].cpsr  = 0x1FU;
    pcb[2].state = READY;
    for (i = 0; i < 13; i++) pcb[2].regs[i] = 0U;
#endif
}

/* ---------------------------------------------------------------
 * Timer IRQ handler  (called from _irq_handler in root.s)
 *
 * On entry:
 *   saved_regs[0..12] = R0-R12 of interrupted process
 *   saved_lr          = LR_irq = interrupted PC + 4
 *   saved_cpsr        = SPSR_irq = interrupted CPSR
 *   CPU is in IRQ mode, IRQs disabled
 *
 * Interrupt clear / EOI:
 *   BBB  : clear TIMER_TISR, then write INTC_CONTROL EOI
 *   QEMU : read GICC_IAR (acknowledge), re-arm Generic Timer,
 *          write GICC_EOIR (end of interrupt) at the end
 * --------------------------------------------------------------- */
void timer_irq_handler(void)
{
    int i;
    unsigned int sys_sp, sys_lr;
    unsigned int new_sp, new_lr;
#ifdef QEMU
    unsigned int iar;
#endif

    /* 1. Acknowledge interrupt source ----------------------------- */
#ifdef QEMU
    /*
     * Reading GICC_IAR acknowledges the interrupt and returns the INTID.
     * Re-arming the Physical Timer (writing CNTP_TVAL) clears ISTATUS
     * and deasserts the level-sensitive interrupt line.
     */
    iar = GICC_IAR;
    asm volatile("mcr p15, 0, %0, c14, c2, 0" :: "r"(timer_freq)); /* re-arm */
    asm volatile("isb");
#else
    /* Clear DMTimer2 overflow flag, then signal EOI to INTC */
    TIMER_TISR  = 0x2U;
    INTC_CONTROL = 0x1U;
#endif

    /* 2. Save current process context from global arrays --------- */
    for (i = 0; i < 13; i++) {
        pcb[current_process].regs[i] = saved_regs[i];
    }
    /* saved_lr = interrupted PC + 4  →  actual PC = saved_lr - 4 */
    pcb[current_process].pc   = saved_lr - 4U;
    pcb[current_process].cpsr = saved_cpsr;

    /*
     * Save System-mode banked SP and LR.
     * We are in IRQ mode; switching temporarily to System mode
     * lets us read the process's SP and LR.
     * IRQs remain disabled (bit 7 forced high during switch).
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

    /* 3. Round-robin schedule ------------------------------------ */
    pcb[current_process].state = READY;
    current_process = (current_process == 1) ? 2 : 1;
    pcb[current_process].state = RUNNING;

    /* 4. Load next process context into global arrays ------------ */
    for (i = 0; i < 13; i++) {
        saved_regs[i] = pcb[current_process].regs[i];
    }
    /*
     * The IRQ return uses:  subs pc, lr, #4
     * To resume at pcb.pc we need  LR_irq = pcb.pc + 4.
     */
    saved_lr   = pcb[current_process].pc + 4U;
    saved_cpsr = pcb[current_process].cpsr;

    /* Restore System-mode SP and LR for the next process */
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

    /* 5. Signal End-of-Interrupt (QEMU only) --------------------- */
#ifdef QEMU
    GICC_EOIR = iar;
#endif
}

/* ---------------------------------------------------------------
 * OS main entry point (called from _start in root.s)
 * --------------------------------------------------------------- */
void main(void)
{
    /* 1. Initialise UART (enables PL011 on QEMU; no-op on BBB) */
    uart_init();

#ifndef QEMU
    /* 2. Disable watchdog — must be done before ~60 s elapses (BBB only) */
    wdt_disable();
#endif

    /* 3. Initialise PCBs */
#ifdef QEMU
    uart_puts("[OS] Running on QEMU virt (Cortex-A15)\r\n");
#else
    uart_puts("[OS] Running on BeagleBone Black (Cortex-A8)\r\n");
#endif
    pcb_init();

    /* 4. Start timer (~1 s period) */
    timer_init();

    /* 5. Configure interrupt controller and enable CPU IRQs */
    intc_init();

    /* 6. Memory barrier before first context switch */
    asm volatile("dsb" ::: "memory");

    /* 7. First context switch: jump directly to P1
     *
     * We are in System mode.  Setting SP and LR here establishes
     * P1's System-mode stack and return address, then PC jumps to
     * the P1 entry point.  When a timer IRQ fires P1 is interrupted,
     * the handler saves its context and resumes P2.
     */
    pcb[1].state  = RUNNING;
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
