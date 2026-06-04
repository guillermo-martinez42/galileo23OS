/*
 * core/pcb.c - Process Control Block management (Phase 2)
 *
 *   BBB  (os.ld)      : P1 @ 0x82100000,  P2 @ 0x82200000
 *   QEMU (os_qemu.ld) : P1 @ 0x40100000,  P2 @ 0x40200000
 *
 * Phase 2: tasks start in USR mode (CPSR = 0x10, F masked, I clear).
 * user_base/user_size describe the inclusive range that sys_write
 * pointer validation accepts (text + stack of the task image).
 */

#include "sched.h"

pcb_t pcb[N_PCB];
int   current_process = 1;

#ifdef QEMU
  #define P1_BASE  0x40100000U
  #define P1_TOP   0x40112000U   /* end of P1 stack (exclusive)  */
  #define P2_BASE  0x40200000U
  #define P2_TOP   0x40212000U
#else
  #define P1_BASE  0x82100000U
  #define P1_TOP   0x82112000U
  #define P2_BASE  0x82200000U
  #define P2_TOP   0x82212000U
#endif

/* USR mode, FIQ masked, IRQ enabled, ARM state */
#define USR_CPSR  0x50U

static void init_user_pcb(int idx,
                          unsigned int pid,
                          unsigned int base,
                          unsigned int top)
{
    int i;

    pcb[idx].pid        = pid;
    pcb[idx].pc         = base;          /* entry point          */
    pcb[idx].sp         = top;           /* top of stack         */
    pcb[idx].lr         = base;          /* return-to-self trap  */
    pcb[idx].cpsr       = USR_CPSR;
    pcb[idx].state      = READY;
    pcb[idx].user_base  = base;
    pcb[idx].user_size  = top - base;
    pcb[idx].syscall_id = 0U;
    pcb[idx].fault      = FAULT_NONE;
    pcb[idx].fault_pc   = 0U;
    pcb[idx].fault_addr = 0U;
    pcb[idx].exit_code  = 0;
    for (i = 0; i < 13; i++)
        pcb[idx].regs[i] = 0U;
}

void pcb_init(void)
{
    /* Slot 0 is reserved (kernel/idle); zero it for cleanliness. */
    pcb[0].pid   = 0U;
    pcb[0].state = TERMINATED;

    init_user_pcb(1, 1U, P1_BASE, P1_TOP);
    init_user_pcb(2, 2U, P2_BASE, P2_TOP);
}
