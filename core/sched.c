/*
 * core/sched.c — Scheduler, IRQ glue, and first-launch (Phase 2)
 *
 * Owns: the shared trap-frame staging (saved_regs/saved_lr/saved_cpsr),
 * the timer-IRQ C glue, the round-robin selection that skips terminated
 * tasks, and the helpers that consume/produce USR banked SP/LR via a
 * brief excursion into System mode.
 *
 * Hardware acknowledge/EOI is delegated to the platform driver via
 * timer_irq_begin() / timer_irq_end() (drivers/{beagle,qemu}/).
 */

#include "sched.h"
#include "drivers.h"
#include "stdio.h"

/* Shared trap-frame staging — written by every exception-mode asm
 * handler in boot startup.s and consumed by the C glue here / in
 * core/syscall.c / core/fault.c.  Single owner at a time because IRQ /
 * SVC / Abort cannot nest (CPU masks IRQ on entry to each and the
 * kernel never re-enables it before exception return). */
volatile unsigned int saved_regs[13];
volatile unsigned int saved_lr;
volatile unsigned int saved_cpsr;

/* ---------------------------------------------------------------
 * USR banked SP/LR access.
 *
 * The interrupted task ran in USR mode; its SP and LR are in the
 * USR/SYS banked register pair (USR and SYS share R13_usr/R14_usr).
 * From a privileged mode we can read/write them by stepping into
 * System mode briefly with IRQs masked.
 * --------------------------------------------------------------- */
/* AAPCS: r0 = *sp_out, r1 = *lr_out.  Written naked to keep total
 * control over register choices — earlier inline-asm versions allowed
 * the compiler to assign saved_mode to `lr`, which then collided with
 * the `mov %[l], lr` instruction reading the USR-banked LR. */
__attribute__((naked))
void read_usr_sp_lr(unsigned int *sp_out __attribute__((unused)),
                    unsigned int *lr_out __attribute__((unused)))
{
    asm volatile(
        "push   {r4, r5, r6, lr}    \n"
        "mrs    r4, cpsr            \n"   /* r4 = caller-mode CPSR */
        "bic    r5, r4, #0x1F       \n"
        "orr    r5, r5, #0x1F       \n"   /* M = System mode      */
        "orr    r5, r5, #0x80       \n"   /* I = 1 (mask IRQs)    */
        "msr    cpsr_c, r5          \n"
        "isb                        \n"
        "mov    r5, sp              \n"   /* r5 = SP_usr (banked) */
        "mov    r6, lr              \n"   /* r6 = LR_usr (banked) */
        "msr    cpsr_c, r4          \n"   /* restore caller mode  */
        "isb                        \n"
        "str    r5, [r0]            \n"
        "str    r6, [r1]            \n"
        "pop    {r4, r5, r6, pc}    \n"
    );
}

/* AAPCS: r0 = sp_in, r1 = lr_in.  Same rationale as read_usr_sp_lr. */
__attribute__((naked))
void write_usr_sp_lr(unsigned int sp_in __attribute__((unused)),
                     unsigned int lr_in __attribute__((unused)))
{
    asm volatile(
        "push   {r4, lr}            \n"
        "mrs    r4, cpsr            \n"
        "bic    r3, r4, #0x1F       \n"
        "orr    r3, r3, #0x1F       \n"
        "orr    r3, r3, #0x80       \n"
        "msr    cpsr_c, r3          \n"
        "isb                        \n"
        "mov    sp, r0              \n"
        "mov    lr, r1              \n"
        "msr    cpsr_c, r4          \n"
        "isb                        \n"
        "pop    {r4, pc}            \n"
    );
}

/* ---------------------------------------------------------------
 * Round-robin selection (skips TERMINATED tasks).
 *
 * Starts the search at the slot AFTER `from_pid` and wraps around;
 * returns the next runnable pid (READY or RUNNING), or 0 if none.
 * --------------------------------------------------------------- */
int pick_next_runnable(int from_pid)
{
    int i, n = from_pid;
    for (i = 0; i < N_PCB; i++) {
        n++;
        if (n >= N_PCB) n = 1;          /* slot 0 reserved */
        if (pcb[n].state == READY || pcb[n].state == RUNNING)
            return n;
    }
    return 0;
}

/* ---------------------------------------------------------------
 * MODE_SWITCH trace helpers (PDF §3.8).  Plain UART output via PRINT.
 * Heavy enough to skew tick alignment if traced every IRQ, but the
 * 1-second period leaves plenty of bandwidth at 115200 baud.
 * --------------------------------------------------------------- */
void trace_initial_launch(int pid)
{
    PRINT("MODE_SWITCH KERNEL_TO_USER pid=%d reason=initial_launch\n", pid);
}
void trace_irq_enter(int pid)
{
    PRINT("MODE_SWITCH USER_TO_KERNEL pid=%d reason=timer_irq\n", pid);
}
void trace_dispatch(int pid)
{
    PRINT("MODE_SWITCH KERNEL_TO_USER pid=%d reason=dispatch\n", pid);
}
void trace_syscall_enter(int pid, int id)
{
    PRINT("MODE_SWITCH USER_TO_KERNEL pid=%d reason=syscall id=%d\n", pid, id);
}
void trace_syscall_return(int pid, int id, int rc)
{
    PRINT("MODE_SWITCH KERNEL_TO_USER pid=%d reason=syscall_return id=%d rc=%d\n",
          pid, id, rc);
}
void trace_fault_enter(int pid, int type)
{
    PRINT("MODE_SWITCH USER_TO_KERNEL pid=%d reason=fault type=%d\n", pid, type);
}
void trace_fault_recovery(int pid)
{
    PRINT("MODE_SWITCH KERNEL_TO_USER pid=%d reason=fault_recovery\n", pid);
}

/* ---------------------------------------------------------------
 * Halt path when every user task has terminated.
 *
 * Called from kernel C code (any privileged mode); does not return.
 * --------------------------------------------------------------- */
void halt_no_runnable(void)
{
    PRINT("[OS] All user tasks terminated; halting.\n");
    for (;;)
        asm volatile("wfi");
}

/* ---------------------------------------------------------------
 * Timer IRQ handler — called from _irq_handler in boot startup.s
 *
 * Convention (PDF §3.5):
 *   saved_lr = LR_irq = (USR resume PC) + 4
 *   saved_cpsr = SPSR_irq (CPSR of interrupted USR task)
 * --------------------------------------------------------------- */
void timer_irq_handler(void)
{
    int          i;
    unsigned int usr_sp, usr_lr;
    unsigned int token;
    int          prev, next;

    token = timer_irq_begin();

    prev = current_process;
    trace_irq_enter(prev);

    /* --- Save current task context --- */
    for (i = 0; i < 13; i++)
        pcb[prev].regs[i] = saved_regs[i];
    pcb[prev].pc   = saved_lr - 4U;
    pcb[prev].cpsr = saved_cpsr;

    read_usr_sp_lr(&usr_sp, &usr_lr);
    pcb[prev].sp = usr_sp;
    pcb[prev].lr = usr_lr;

    if (pcb[prev].state == RUNNING)
        pcb[prev].state = READY;

    /* --- Schedule next runnable task --- */
    next = pick_next_runnable(prev);
    if (next == 0) {
        timer_irq_end(token);
        halt_no_runnable();
    }
    current_process = next;
    pcb[next].state = RUNNING;

    /* --- Load next task context --- */
    for (i = 0; i < 13; i++)
        saved_regs[i] = pcb[next].regs[i];
    saved_lr   = pcb[next].pc + 4U;     /* asm does subs pc, lr, #4 */
    saved_cpsr = pcb[next].cpsr;
    write_usr_sp_lr(pcb[next].sp, pcb[next].lr);

    trace_dispatch(next);

    timer_irq_end(token);
}

/* ---------------------------------------------------------------
 * OS main — bring-up and first-launch (Phase 2 §3.4)
 * --------------------------------------------------------------- */
void main(void)
{
    uart_init();

#ifndef QEMU
    wdt_disable();
#endif

#ifdef QEMU
    uart_puts("[OS] Running on QEMU virt (Cortex-A15)\r\n");
#else
    uart_puts("[OS] Running on BeagleBone Black (Cortex-A8)\r\n");
#endif

    pcb_init();
    timer_init();
    intc_init();

    asm volatile("dsb" ::: "memory");

    /* First-launch: P1 enters USR via exception return. */
    pcb[1].state    = RUNNING;
    current_process = 1;

    trace_initial_launch(1);

    launch_first_task(pcb[1].pc, pcb[1].sp, pcb[1].cpsr);

    /* Never reached — launch_first_task does not return. */
    while (1) ;
}
