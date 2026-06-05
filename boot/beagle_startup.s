@ boot/beagle_startup.s — ARM Bare-Metal OS Entry Point (Phase 2, BeagleBone Black AM335x)
@
@ Handles: CPU mode setup, banked stacks for every privileged mode,
@ BSS clear, VBAR install, vector table, and real handlers for:
@   IRQ            — timer preemption       → timer_irq_handler()
@   SVC            — syscall trap           → syscall_dispatch()
@   Prefetch abort — user-mode code fault   → fault_handler(FAULT_PREFETCH)
@   Data abort     — user-mode data fault   → fault_handler(FAULT_DATA)
@
@ All save/restore paths funnel through three shared globals (Phase 2 §6
@ "trap-frame anchor"): saved_regs[13], saved_lr, saved_cpsr.  The C side
@ translates between raw LR_<mode> and the resume PC per path.
@
@ Stack-top symbols are provided by the linker script (os.ld):
@   SYS_STACK_TOP / IRQ_STACK_TOP / SVC_STACK_TOP / ABT_STACK_TOP / UND_STACK_TOP

.extern main
.extern timer_irq_handler
.extern syscall_dispatch
.extern fault_handler
.extern saved_regs
.extern saved_lr
.extern saved_cpsr

.equ FAULT_PREFETCH, 1
.equ FAULT_DATA,     2

.section .text
.global _start
.global launch_first_task

@ ---------------------------------------------------------------
@ Entry point
@ ---------------------------------------------------------------
_start:
    @ ---- Set IRQ-mode banked stack ----
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0xD2           @ IRQ mode, I=1, F=1
    msr     cpsr_c, r0
    ldr     sp, =IRQ_STACK_TOP

    @ ---- Set SVC-mode banked stack ----
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x13           @ SVC mode (keeps I=1, F=1)
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    ldr     sp, =SVC_STACK_TOP

    @ ---- Set ABT-mode banked stack ----
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x17           @ Abort mode
    msr     cpsr_c, r0
    ldr     sp, =ABT_STACK_TOP

    @ ---- Set UND-mode banked stack ----
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x1B           @ Undefined mode
    msr     cpsr_c, r0
    ldr     sp, =UND_STACK_TOP

    @ ---- Finally enter System mode for kernel C runtime ----
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x1F           @ System mode, I=1, F=1
    msr     cpsr_c, r0
    ldr     sp, =SYS_STACK_TOP

    @ ---- Clear .bss section ----
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    mov     r2, #0
bss_clear_loop:
    cmp     r0, r1
    strlt   r2, [r0], #4
    blt     bss_clear_loop

    @ ---- Memory barriers after BSS clear ----
    dsb
    isb

    @ ---- Install vector table via VBAR ----
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0
    isb

    @ ---- Jump to C main ----
    bl      main

    @ ---- Should never return ----
_halt:
    wfi
    b       _halt


@ ---------------------------------------------------------------
@ Vector Table  (must be 32-byte / 5-bit aligned)
@ ---------------------------------------------------------------
    .align  5
_vector_table:
    ldr     pc, =_reset_handler         @ 0x00  Reset
    ldr     pc, =_undef_handler         @ 0x04  Undefined Instruction
    ldr     pc, =_svc_handler           @ 0x08  SVC / SWI
    ldr     pc, =_prefetch_handler      @ 0x0C  Prefetch Abort
    ldr     pc, =_data_handler          @ 0x10  Data Abort
    nop                                  @ 0x14  Reserved
    ldr     pc, =_irq_handler           @ 0x18  IRQ
    ldr     pc, =_fiq_handler           @ 0x1C  FIQ


@ ---------------------------------------------------------------
@ launch_first_task(unsigned entry, unsigned usr_sp, unsigned usr_cpsr)
@
@ Called once from main() running in System mode.  Establishes a USR
@ context via the same exception-return mechanism the kernel uses on
@ every later schedule decision (PDF §3.4):
@   1. From SYS mode, write SP_usr (banked, shared with SYS) and LR_usr.
@   2. Switch to SVC mode.  Set SPSR_svc = USR CPSR, LR_svc = entry.
@   3. movs pc, lr  — atomic CPSR<-SPSR, PC<-LR, lands in USR @ entry.
@
@ The SYS-mode kernel stack is intentionally dropped after this; later
@ kernel work runs on the privileged banked stacks (IRQ/SVC/ABT/UND).
@ ---------------------------------------------------------------
.type launch_first_task, %function
launch_first_task:
    @ r0 = entry, r1 = usr_sp, r2 = usr_cpsr

    @ Switch to SYS to write the USR-banked SP/LR.
    mrs     r3, cpsr
    bic     r3, r3, #0x1F
    orr     r3, r3, #0x1F           @ SYS mode
    orr     r3, r3, #0x80           @ keep IRQ masked
    msr     cpsr_c, r3
    isb
    mov     sp, r1                  @ SP_usr  = stack top
    mov     lr, r0                  @ LR_usr  = entry (return-to-self)

    @ Move to SVC and build the exception-return frame.
    bic     r3, r3, #0x1F
    orr     r3, r3, #0x13           @ SVC mode
    msr     cpsr_c, r3
    isb
    msr     spsr_cxsf, r2           @ SPSR_svc = usr_cpsr (USR + I clear)
    mov     lr, r0                  @ LR_svc   = entry
    movs    pc, lr                  @ exception return -> USR @ entry


@ ---------------------------------------------------------------
@ Default / stub exception handlers
@ ---------------------------------------------------------------
_reset_handler:
    b       _start

_undef_handler:
    b       _undef_handler

_fiq_handler:
    b       _fiq_handler


@ ---------------------------------------------------------------
@ Macros for trap-frame save/restore through the shared staging area.
@ Each handler funnels through the same shape so that C glue handles
@ SP_usr/LR_usr and PC math in one place.
@
@   On entry to a macro the CPU is already in the exception mode and:
@     R0-R12  = USR-context registers
@     LR_<m>  = raw exception link register (mode-specific offset)
@     SPSR_<m>= CPSR of the interrupted/calling task
@ ---------------------------------------------------------------
.macro SAVE_TRAP_FRAME
    push    {r0, lr}                @ stash R0 + LR_<mode>
    mrs     r0, spsr
    ldr     lr, =saved_cpsr
    str     r0, [lr]
    ldr     lr, =saved_regs
    add     lr, lr, #4              @ saved_regs[1]
    stmia   lr, {r1-r12}
    ldr     r0, [sp, #0]            @ original R0
    ldr     lr, =saved_regs
    str     r0, [lr, #0]
    ldr     r0, [sp, #4]            @ LR_<mode>
    ldr     lr, =saved_lr
    str     r0, [lr]
    add     sp, sp, #8
.endm

.macro LOAD_TRAP_FRAME
    ldr     r0, =saved_cpsr
    ldr     r0, [r0]
    msr     spsr_cxsf, r0
    ldr     lr, =saved_lr
    ldr     lr, [lr]
    ldr     r0, =saved_regs
    ldmia   r0, {r0-r12}
.endm


@ ---------------------------------------------------------------
@ IRQ Handler (asynchronous preemption)
@
@ LR_irq = interrupted_PC + 4  → asm captures as-is; sched.c does the -4
@ math.  Returns via subs pc, lr, #4 with LR pre-loaded to pcb.pc + 4.
@ ---------------------------------------------------------------
_irq_handler:
    SAVE_TRAP_FRAME
    bl      timer_irq_handler
    LOAD_TRAP_FRAME
    subs    pc, lr, #4


@ ---------------------------------------------------------------
@ SVC Handler (syscall trap)
@
@ LR_svc = USR_PC of instruction AFTER the svc — i.e. the resume PC.
@ asm stores it raw; syscall_dispatch uses it as-is.  Return is
@ movs pc, lr with LR pre-loaded to pcb.pc.
@ ---------------------------------------------------------------
_svc_handler:
    SAVE_TRAP_FRAME
    bl      syscall_dispatch
    LOAD_TRAP_FRAME
    movs    pc, lr


@ ---------------------------------------------------------------
@ Prefetch Abort Handler
@
@ LR_abt = faulted_PC + 4 (per ARMv7-A).  fault.c reads it as-is and
@ records pcb.fault_pc = saved_lr - 4 for the offending task, then
@ dispatches a different task with saved_lr = next.pc + 4 so this
@ handler's "subs pc, lr, #4" lands at the new task's resume PC.
@ ---------------------------------------------------------------
_prefetch_handler:
    SAVE_TRAP_FRAME
    mov     r0, #FAULT_PREFETCH
    bl      fault_handler
    LOAD_TRAP_FRAME
    subs    pc, lr, #4


@ ---------------------------------------------------------------
@ Data Abort Handler
@
@ LR_abt = faulted_PC + 8 (per ARMv7-A).  fault.c records
@ pcb.fault_pc = saved_lr - 8 for the offender and prepares the
@ replacement task with saved_lr = next.pc + 4 (using the same -4 idiom
@ as the IRQ/prefetch paths for symmetric restore — the offset is just
@ an arithmetic adjustment we control on both ends).
@ ---------------------------------------------------------------
_data_handler:
    SAVE_TRAP_FRAME
    mov     r0, #FAULT_DATA
    bl      fault_handler
    LOAD_TRAP_FRAME
    subs    pc, lr, #4
