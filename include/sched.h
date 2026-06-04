#ifndef SCHED_H
#define SCHED_H

/* ---------------------------------------------------------------
 * Number of PCB slots: [0]=kernel/idle (unused), [1]=P1, [2]=P2
 * --------------------------------------------------------------- */
#define N_PCB 3

/* ---------------------------------------------------------------
 * Process states
 * --------------------------------------------------------------- */
typedef enum {
    READY,
    RUNNING,
    WAITING,
    TERMINATED
} proc_state_t;

/* ---------------------------------------------------------------
 * Fault classification (Phase 2 §5)
 * --------------------------------------------------------------- */
typedef enum {
    FAULT_NONE     = 0,
    FAULT_PREFETCH = 1,   /* Prefetch abort                       */
    FAULT_DATA     = 2,   /* Data abort                            */
    FAULT_UNDEF    = 3    /* Undefined instruction (reserved)      */
} fault_type_t;

/* ---------------------------------------------------------------
 * Process Control Block
 *
 * The PCB holds the full USR-mode resume image plus the bookkeeping
 * needed by syscall, IRQ, and fault paths (PDF §6).
 * --------------------------------------------------------------- */
typedef struct {
    unsigned int  pid;
    unsigned int  regs[13];     /* R0-R12                          */
    unsigned int  sp;           /* USR banked SP                   */
    unsigned int  lr;           /* USR banked LR                   */
    unsigned int  pc;           /* Resume address                  */
    unsigned int  cpsr;         /* Saved CPSR (USR mode, I clear)  */
    proc_state_t  state;

    /* Memory range that USR pointers must fall inside (syscall valid.) */
    unsigned int  user_base;
    unsigned int  user_size;

    /* Syscall transient state */
    unsigned int  syscall_id;   /* Last syscall id (for logging)   */

    /* Fault / termination bookkeeping */
    fault_type_t  fault;
    unsigned int  fault_pc;
    unsigned int  fault_addr;
    int           exit_code;
} pcb_t;

/* PCB array and scheduler state (defined in core/pcb.c) */
extern pcb_t pcb[N_PCB];
extern int   current_process;

/*
 * Shared trap-frame staging area written by every exception-mode
 * assembly handler and consumed by the C glue.  Single owner at a time
 * because IRQ/SVC/Abort are mutually exclusive (CPU masks IRQ on entry
 * to each, and the kernel never re-enables it before returning).
 *
 *   saved_regs : R0-R12 of the interrupted USR context
 *   saved_lr   : raw LR_<mode> as the asm captured it (path-specific)
 *   saved_cpsr : SPSR_<mode> (CPSR of the USR caller / interrupted task)
 *
 * Per-path conversion to/from pcb_t.pc happens in C (see core/sched.c,
 * core/syscall.c, core/fault.c).  Conventions:
 *
 *   IRQ      : asm stores LR_irq (= USR_PC + 4) and returns subs pc,lr,#4
 *   SVC      : asm stores LR_svc (= USR_PC of next instr) and returns movs pc,lr
 *   PREFETCH : asm stores LR_abt (= faulted_PC + 4) and returns subs pc,lr,#4
 *   DATA     : asm stores LR_abt (= faulted_PC + 8) and returns subs pc,lr,#4
 *               (data abort path repurposes lr post-recovery; we set
 *                lr = next_task.pc + 4 to match the +4 offset on return.)
 */
extern volatile unsigned int saved_regs[13];
extern volatile unsigned int saved_lr;
extern volatile unsigned int saved_cpsr;

/* core/pcb.c */
void pcb_init(void);

/* core/sched.c */
void timer_irq_handler(void);   /* called from _irq_handler asm     */
int  pick_next_runnable(int from_pid);
void halt_no_runnable(void);    /* never returns                    */

/* Read/write USR banked SP/LR from a privileged mode (briefly enters
 * System mode while keeping IRQs masked). */
void read_usr_sp_lr(unsigned int *sp, unsigned int *lr);
void write_usr_sp_lr(unsigned int sp, unsigned int lr);

/* core/syscall.c — called from _svc_handler asm */
void syscall_dispatch(void);

/* core/fault.c — called from _prefetch_handler / _data_handler asm */
void fault_handler(int fault_type);

/* core/trace.c (folded into syscall.c) — MODE_SWITCH log lines */
void trace_initial_launch(int pid);
void trace_irq_enter      (int pid);
void trace_dispatch       (int pid);
void trace_syscall_enter  (int pid, int id);
void trace_syscall_return (int pid, int id, int rc);
void trace_fault_enter    (int pid, int type);
void trace_fault_recovery (int pid);

/* startup.s — naked first-launch helper (sets SP_usr, SPSR=USR,
 * LR_svc=entry, then movs pc,lr).  Does not return. */
void launch_first_task(unsigned int entry,
                       unsigned int usr_sp,
                       unsigned int usr_cpsr) __attribute__((noreturn));

#endif /* SCHED_H */
