/*
 * core/fault.c — Prefetch / data abort handler (Phase 2 §5)
 *
 * Called from _prefetch_handler / _data_handler in boot startup.s
 * after the shared trap frame is staged.  Job:
 *   1. Identify and mark the faulting task TERMINATED (per fault policy).
 *   2. Record fault classification + faulting PC + faulting address.
 *   3. Emit MODE_SWITCH fault / fault_recovery traces.
 *   4. Round-robin to the next runnable USR task; halt if none.
 *   5. Pack the chosen task's frame for the abort handler's
 *      `subs pc, lr, #4` exception return.
 *
 * Without an MMU, faults here typically come from bad load/store/branch
 * to unmapped or peripheral-only addresses on the AXI bus (e.g. user
 * dereferencing 0xFFFFFFFF).  IFSR/DFSR are still populated for
 * observability.
 */

#include "sched.h"
#include "stdio.h"

static unsigned int read_ifsr(void)
{
    unsigned int v;
    asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(v));
    return v;
}

static unsigned int read_dfsr(void)
{
    unsigned int v;
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(v));
    return v;
}

static unsigned int read_ifar(void)
{
    unsigned int v;
    asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(v));
    return v;
}

static unsigned int read_dfar(void)
{
    unsigned int v;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(v));
    return v;
}

void fault_handler(int fault_type)
{
    int          caller = current_process;
    int          next;
    int          i;
    unsigned int usr_sp, usr_lr;
    unsigned int fault_pc, fault_addr, fsr;

    /* --- Compute the original faulting PC and fault address --- */
    if (fault_type == FAULT_DATA) {
        fault_pc   = saved_lr - 8U;   /* data abort: LR_abt = PC + 8 */
        fault_addr = read_dfar();
        fsr        = read_dfsr();
    } else { /* FAULT_PREFETCH (or anything else mapped here) */
        fault_pc   = saved_lr - 4U;   /* prefetch abort: LR_abt = PC + 4 */
        fault_addr = read_ifar();
        fsr        = read_ifsr();
    }

    /* --- Snapshot caller registers + USR banked SP/LR --- */
    for (i = 0; i < 13; i++)
        pcb[caller].regs[i] = saved_regs[i];
    pcb[caller].pc   = fault_pc;
    pcb[caller].cpsr = saved_cpsr;
    read_usr_sp_lr(&usr_sp, &usr_lr);
    pcb[caller].sp = usr_sp;
    pcb[caller].lr = usr_lr;

    /* --- Mark TERMINATED and record diagnostic info --- */
    pcb[caller].state      = TERMINATED;
    pcb[caller].fault      = (fault_type_t)fault_type;
    pcb[caller].fault_pc   = fault_pc;
    pcb[caller].fault_addr = fault_addr;
    pcb[caller].exit_code  = -1;

    trace_fault_enter(caller, fault_type);
    PRINT("[OS] pid=%d fault type=%d pc=%x addr=%x fsr=%x — terminated\n",
          caller, fault_type, fault_pc, fault_addr, fsr);

    /* --- Schedule next runnable task; halt if none --- */
    next = pick_next_runnable(caller);
    if (next == 0)
        halt_no_runnable();

    current_process = next;
    pcb[next].state = RUNNING;

    /* --- Load next frame; asm returns via subs pc, lr, #4. --- */
    for (i = 0; i < 13; i++)
        saved_regs[i] = pcb[next].regs[i];
    saved_lr   = pcb[next].pc + 4U;
    saved_cpsr = pcb[next].cpsr;
    write_usr_sp_lr(pcb[next].sp, pcb[next].lr);

    trace_fault_recovery(next);
}
