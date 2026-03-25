/*
 * drivers/qemu/virt_timer.c
 *
 * QEMU -M virt (ARM Cortex-A15) peripheral drivers:
 *   ARM GICv2         — Generic Interrupt Controller
 *   ARM Generic Timer — Physical Non-Secure Timer (CP15, INTID 30)
 *
 * This file is compiled only for the QEMU target (-DQEMU flag).
 * The #ifdef guard makes it safe if the file is accidentally included
 * in a BBB build.
 */
#ifdef QEMU

/* ---------------------------------------------------------------
 * ARM GICv2 registers
 *   Distributor base : 0x08000000
 *   CPU interface    : 0x08010000
 * --------------------------------------------------------------- */
#define GICD_BASE        0x08000000U
#define GICC_BASE        0x08010000U

/* GIC Distributor */
#define GICD_CTLR       (*(volatile unsigned int *)(GICD_BASE + 0x000U)) /* Enable distributor    */
#define GICD_ISENABLER0 (*(volatile unsigned int *)(GICD_BASE + 0x100U)) /* Enable SGI/PPI 0-31  */

/* GIC CPU Interface */
#define GICC_CTLR (*(volatile unsigned int *)(GICC_BASE + 0x000U)) /* Enable CPU interface  */
#define GICC_PMR  (*(volatile unsigned int *)(GICC_BASE + 0x004U)) /* Priority mask         */
#define GICC_IAR  (*(volatile unsigned int *)(GICC_BASE + 0x00CU)) /* Interrupt Acknowledge */
#define GICC_EOIR (*(volatile unsigned int *)(GICC_BASE + 0x010U)) /* End of Interrupt      */

/*
 * ARM Generic Timer — Physical Non-Secure Timer
 *   INTID 30 = PPI 14  →  bit 30 of GICD_ISENABLER0
 *
 * CP15 registers (Cortex-A15):
 *   CNTFRQ    c14,c0,0 — counter frequency in Hz (read-only from EL1)
 *   CNTP_TVAL c14,c2,0 — physical countdown value (write to re-arm)
 *   CNTP_CTL  c14,c2,1 — control: bit0=ENABLE, bit1=IMASK, bit2=ISTATUS
 */
#define GTIMER_INTID 30U

/* Frequency captured once in timer_init(); used again in timer_irq_begin() */
static unsigned int timer_freq;

/* ---------------------------------------------------------------
 * timer_init — configure the ARM Generic Timer for 1 s interrupts
 *
 * 1. Read CNTFRQ to learn the system counter tick rate.
 * 2. Write CNTP_TVAL with that count (= 1 second of ticks).
 * 3. Enable the timer with IMASK=0 so the interrupt fires.
 * --------------------------------------------------------------- */
void timer_init(void)
{
    /* Read system counter frequency (ticks/second) */
    asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(timer_freq));

    /* Load countdown: CNTFRQ ticks = exactly 1 second */
    asm volatile("mcr p15, 0, %0, c14, c2, 0" :: "r"(timer_freq));
    asm volatile("isb");

    /* Enable timer, interrupt not masked (ENABLE=1, IMASK=0) */
    asm volatile("mcr p15, 0, %0, c14, c2, 1" :: "r"(0x1U));
    asm volatile("isb");
}

/* ---------------------------------------------------------------
 * intc_init — configure ARM GICv2 and enable the Generic Timer PPI
 *
 * Steps:
 *   1. Enable GIC Distributor
 *   2. Enable PPI 14 (INTID 30, bit 30 of GICD_ISENABLER0)
 *   3. Set priority mask to 0xFF (accept all interrupt priorities)
 *   4. Enable GIC CPU Interface
 *   5. Clear I-bit in CPSR so IRQs reach the CPU
 * --------------------------------------------------------------- */
void intc_init(void)
{
    GICD_CTLR       = 1U;
    GICD_ISENABLER0 = (1U << GTIMER_INTID);
    GICC_PMR        = 0xFFU;
    GICC_CTLR       = 1U;

    /* Enable IRQs in CPU: clear I-bit in CPSR */
    asm volatile(
        "mrs r0, cpsr\n"
        "bic r0, r0, #0x80\n"
        "msr cpsr_c, r0\n"
        ::: "r0"
    );
}

/* ---------------------------------------------------------------
 * timer_irq_begin — acknowledge GIC + re-arm Generic Timer
 *
 * Reading GICC_IAR acknowledges the interrupt (GIC deasserts the line
 * to the CPU) and returns the INTID.  Writing CNTP_TVAL reloads the
 * countdown and clears the ISTATUS bit, deassigning the level-sensitive
 * PPI signal from the GIC distributor.
 *
 * Returns the INTID (IAR value) which must be passed to timer_irq_end().
 * --------------------------------------------------------------- */
unsigned int timer_irq_begin(void)
{
    unsigned int iar = GICC_IAR;                        /* Acknowledge  */
    asm volatile("mcr p15, 0, %0, c14, c2, 0"          /* Re-arm timer */
                 :: "r"(timer_freq));
    asm volatile("isb");
    return iar;
}

/* ---------------------------------------------------------------
 * timer_irq_end — signal End-of-Interrupt to GIC CPU Interface
 *
 * Writing the original IAR value to GICC_EOIR tells the GIC that the
 * handler is done and it may forward the next interrupt of this priority.
 * --------------------------------------------------------------- */
void timer_irq_end(unsigned int token)
{
    GICC_EOIR = token;
}

#endif /* QEMU */
