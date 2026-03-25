#ifndef OS_H
#define OS_H

/* Process states */
typedef enum {
    READY,
    RUNNING
} proc_state_t;

/* Process Control Block */
typedef struct {
    unsigned int  pid;
    unsigned int  regs[13]; /* R0-R12                          */
    unsigned int  sp;       /* R13 (System-mode banked)        */
    unsigned int  lr;       /* R14 (System-mode banked)        */
    unsigned int  pc;       /* Resume address                  */
    unsigned int  cpsr;     /* Saved CPSR / SPSR               */
    proc_state_t  state;
} pcb_t;

/* PCB array and scheduler state (defined in os.c) */
extern pcb_t pcb[3];
extern int   current_process;

/*
 * Global register save areas shared between root.s (assembly IRQ handler)
 * and os.c (C context-switch logic).
 */
extern volatile unsigned int saved_regs[13]; /* R0-R12 of interrupted process */
extern volatile unsigned int saved_lr;       /* LR_irq  = interrupted PC + 4  */
extern volatile unsigned int saved_cpsr;     /* SPSR_irq = interrupted CPSR   */

/* Called from assembly _irq_handler */
void timer_irq_handler(void);

#endif /* OS_H */
