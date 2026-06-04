/*
 * core/syscall.c — SVC dispatcher (Phase 2 §4)
 *
 * Entered from _svc_handler in boot startup.s.  The shared trap-frame
 * staging holds the caller's USR-context: saved_regs[0..3] = r0..r3
 * (syscall id + arguments), saved_lr = LR_svc (resume PC), saved_cpsr =
 * SPSR_svc (caller's USR CPSR).
 *
 * Dispatcher contract:
 *   1. Save caller's full USR image into its PCB (including banked SP/LR).
 *   2. Verify caller was in USR.  Anything else is treated as bad-ID.
 *   3. Run the per-syscall handler, write rc into the *destination*
 *      task's saved r0.  Schedule (round-robin) if the call yields/exits;
 *      stay with the caller for write.
 *   4. Load chosen task's image back into staging — asm restores and
 *      executes `movs pc, lr` to land in USR at the resume PC.
 */

#include "sched.h"
#include "stdio.h"
#include "user_syscalls.h"

#define USR_MODE_BITS  0x10U
#define MAX_WRITE      4096U

/* ---------------------------------------------------------------
 * Validate that a USR pointer + length fits inside the task's
 * declared user_base..user_base+user_size region, with no overflow.
 * Returns 0 on success, -3 on violation.
 * --------------------------------------------------------------- */
static int validate_user_buffer(int pid,
                                unsigned int buf,
                                unsigned int len)
{
    unsigned int base = pcb[pid].user_base;
    unsigned int size = pcb[pid].user_size;
    unsigned int end_excl = base + size;
    unsigned int buf_end;

    if (buf < base)            return E_BAD_PTR;
    if (buf >= end_excl)       return E_BAD_PTR;
    buf_end = buf + len;
    if (buf_end < buf)         return E_BAD_PTR;  /* overflow */
    if (buf_end > end_excl)    return E_BAD_PTR;
    return 0;
}

/* ---------------------------------------------------------------
 * SYS_WRITE handler — Phase 2 §4.7
 * --------------------------------------------------------------- */
static int do_sys_write(int caller,
                        int fd,
                        unsigned int buf,
                        unsigned int len)
{
    int rc;
    unsigned int i;
    const unsigned char *p;

    if (fd != 1)            return E_BAD_ARG;
    if (len > MAX_WRITE)    return E_BAD_ARG;

    rc = validate_user_buffer(caller, buf, len);
    if (rc < 0) return rc;

    p = (const unsigned char *)buf;
    for (i = 0; i < len; i++)
        uart_putc((char)p[i]);

    return (int)len;
}

/* ---------------------------------------------------------------
 * SVC dispatcher entry — called from _svc_handler asm
 * --------------------------------------------------------------- */
void syscall_dispatch(void)
{
    int          caller = current_process;
    unsigned int id     = saved_regs[0];
    unsigned int a1     = saved_regs[1];
    unsigned int a2     = saved_regs[2];
    unsigned int a3     = saved_regs[3];
    int          next;
    unsigned int usr_sp, usr_lr;
    int          i;
    int          rc;

    /* --- 1. Save caller context --- */
    for (i = 0; i < 13; i++)
        pcb[caller].regs[i] = saved_regs[i];
    pcb[caller].pc         = saved_lr;          /* SVC: LR_svc = resume PC */
    pcb[caller].cpsr       = saved_cpsr;
    read_usr_sp_lr(&usr_sp, &usr_lr);
    pcb[caller].sp         = usr_sp;
    pcb[caller].lr         = usr_lr;
    pcb[caller].syscall_id = id;

    trace_syscall_enter(caller, (int)id);

    /* --- 2. Validate caller mode --- */
    if ((saved_cpsr & 0x1FU) != USR_MODE_BITS) {
        /* Trap came from a privileged mode — refuse. */
        pcb[caller].regs[0] = (unsigned int)E_BAD_ID;
        next = caller;
    } else {
        /* --- 3. Dispatch --- */
        switch (id) {
        case SYS_YIELD:
            pcb[caller].regs[0] = 0;
            if (pcb[caller].state == RUNNING)
                pcb[caller].state = READY;
            next = pick_next_runnable(caller);
            if (next == 0)
                halt_no_runnable();
            break;

        case SYS_EXIT:
            pcb[caller].state     = TERMINATED;
            pcb[caller].exit_code = (int)a1;
            PRINT("[OS] pid=%d exited with code=%d\n", caller, (int)a1);
            next = pick_next_runnable(caller);
            if (next == 0)
                halt_no_runnable();
            break;

        case SYS_WRITE:
            rc = do_sys_write(caller, (int)a1, a2, a3);
            pcb[caller].regs[0] = (unsigned int)rc;
            next = caller;
            break;

        default:
            pcb[caller].regs[0] = (unsigned int)E_BAD_ID;
            next = caller;
            break;
        }
    }

    /* --- 4. Load chosen task's frame back into staging --- */
    current_process = next;
    pcb[next].state = RUNNING;

    for (i = 0; i < 13; i++)
        saved_regs[i] = pcb[next].regs[i];
    saved_lr   = pcb[next].pc;       /* SVC return: movs pc, lr  */
    saved_cpsr = pcb[next].cpsr;
    write_usr_sp_lr(pcb[next].sp, pcb[next].lr);

    trace_syscall_return(next, (int)id, (int)pcb[next].regs[0]);
}
