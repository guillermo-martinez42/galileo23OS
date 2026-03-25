/*
 * core/pcb.c - Process Control Block management
 *
 * Owns the PCB array and populates it with the fixed load addresses
 * defined by the per-platform linker scripts in ldscripts/.
 *
 *   BBB  (os.ld)      : P1 @ 0x82100000,  P2 @ 0x82200000
 *   QEMU (os_qemu.ld) : P1 @ 0x40100000,  P2 @ 0x40200000
 */

#include "sched.h"

pcb_t pcb[3];          /* [0]=OS (unused), [1]=P1, [2]=P2 */
int   current_process = 1;

void pcb_init(void)
{
    int i;

#ifdef QEMU
    /* P1 — prints digits 0-9 */
    pcb[1].pid   = 1;
    pcb[1].pc    = 0x40100000U;   /* Entry point  (p1_qemu.ld) */
    pcb[1].sp    = 0x40112000U;   /* Top of P1 stack           */
    pcb[1].lr    = 0x40100000U;
    pcb[1].cpsr  = 0x1FU;         /* System mode, IRQs on      */
    pcb[1].state = READY;
    for (i = 0; i < 13; i++) pcb[1].regs[i] = 0U;

    /* P2 — prints letters a-z */
    pcb[2].pid   = 2;
    pcb[2].pc    = 0x40200000U;   /* Entry point  (p2_qemu.ld) */
    pcb[2].sp    = 0x40212000U;   /* Top of P2 stack           */
    pcb[2].lr    = 0x40200000U;
    pcb[2].cpsr  = 0x1FU;
    pcb[2].state = READY;
    for (i = 0; i < 13; i++) pcb[2].regs[i] = 0U;
#else
    /* P1 — prints digits 0-9 */
    pcb[1].pid   = 1;
    pcb[1].pc    = 0x82100000U;   /* Entry point  (p1.ld)      */
    pcb[1].sp    = 0x82112000U;   /* Top of P1 stack           */
    pcb[1].lr    = 0x82100000U;
    pcb[1].cpsr  = 0x1FU;         /* System mode, IRQs on      */
    pcb[1].state = READY;
    for (i = 0; i < 13; i++) pcb[1].regs[i] = 0U;

    /* P2 — prints letters a-z */
    pcb[2].pid   = 2;
    pcb[2].pc    = 0x82200000U;   /* Entry point  (p2.ld)      */
    pcb[2].sp    = 0x82212000U;   /* Top of P2 stack           */
    pcb[2].lr    = 0x82200000U;
    pcb[2].cpsr  = 0x1FU;
    pcb[2].state = READY;
    for (i = 0; i < 13; i++) pcb[2].regs[i] = 0U;
#endif
}
