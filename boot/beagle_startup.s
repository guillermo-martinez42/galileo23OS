@ root.s - ARM Bare-Metal OS Entry Point
@ Handles: CPU mode setup, BSS clear, VBAR install, vector table, IRQ handler.
@
@ Stack top addresses are supplied by the linker script as symbols:
@   SYS_STACK_TOP  — System-mode stack top
@   IRQ_STACK_TOP  — IRQ-mode stack top
@
@ BeagleBone Black (os.ld) : SYS_STACK_TOP=0x82012000  IRQ_STACK_TOP=0x82014000
@ QEMU virt     (os_qemu.ld): SYS_STACK_TOP=0x40012000  IRQ_STACK_TOP=0x40014000

.extern main
.extern timer_irq_handler
.extern saved_regs
.extern saved_lr
.extern saved_cpsr

.section .text
.global _start

@ ---------------------------------------------------------------
@ Entry point
@ ---------------------------------------------------------------
_start:
    @ ---- Set System mode (0x1F), IRQ+FIQ disabled (I=1, F=1) ----
    mrs     r0, cpsr
    bic     r0, r0, #0xFF           @ Clear mode + I + F bits
    orr     r0, r0, #0xDF           @ System mode=0x1F, I=1(0x80), F=1(0x40)
    msr     cpsr_c, r0
    ldr     sp, =SYS_STACK_TOP      @ System-mode stack pointer (from linker script)

    @ ---- Set up IRQ-mode banked stack ----
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0xD2           @ IRQ mode (0x12), I=1, F=1
    msr     cpsr_c, r0
    ldr     sp, =IRQ_STACK_TOP      @ IRQ-mode stack pointer (from linker script)

    @ ---- Return to System mode ----
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0xDF           @ System mode, I=1, F=1
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
@ Default / stub exception handlers
@ ---------------------------------------------------------------
_reset_handler:
    b       _start

_undef_handler:
    b       _undef_handler

_svc_handler:
    movs    pc, lr

_prefetch_handler:
    b       _prefetch_handler

_data_handler:
    b       _data_handler

_fiq_handler:
    b       _fiq_handler


@ ---------------------------------------------------------------
@ IRQ Handler
@
@ On entry (IRQ mode):
@   R0-R12  = interrupted process register values
@   LR_irq  = interrupted PC + 4
@   SPSR_irq= interrupted CPSR
@   SP_irq  = IRQ stack (set in _start above)
@
@ Strategy:
@   1. Push {R0, LR} onto IRQ stack      (saves original R0 and LR_irq)
@   2. Save SPSR -> saved_cpsr
@   3. Save R1-R12 -> saved_regs[1..12]
@   4. Recover original R0 from stack -> saved_regs[0]
@   5. Recover LR_irq from stack       -> saved_lr
@   6. Free IRQ stack (add sp, #8)
@   7. Call timer_irq_handler() in C
@   8. Restore SPSR from saved_cpsr
@   9. Load LR_irq from saved_lr
@  10. Restore R0-R12 from saved_regs
@  11. subs pc, lr, #4  (return + restore CPSR from SPSR)
@ ---------------------------------------------------------------
_irq_handler:

    @ --- Step 1: save original R0 and LR_irq onto IRQ stack ---
    @ STMDB (push) stores lower-numbered reg at lower address:
    @   [SP-8] = R0  (original),  [SP-4] = LR  (LR_irq)
    push    {r0, lr}

    @ --- Step 2: save SPSR_irq -> saved_cpsr ---
    mrs     r0, spsr
    ldr     lr, =saved_cpsr
    str     r0, [lr]

    @ --- Step 3: save R1-R12 -> saved_regs[1..12] ---
    ldr     lr, =saved_regs
    add     lr, lr, #4              @ point at saved_regs[1]
    stmia   lr, {r1-r12}

    @ --- Step 4: save original R0 (at [sp+0]) -> saved_regs[0] ---
    ldr     r0, [sp, #0]            @ original R0
    ldr     lr, =saved_regs
    str     r0, [lr, #0]

    @ --- Step 5: save LR_irq (at [sp+4]) -> saved_lr ---
    ldr     r0, [sp, #4]            @ LR_irq = interrupted PC + 4
    ldr     lr, =saved_lr
    str     r0, [lr]

    @ --- Step 6: free IRQ stack frame ---
    add     sp, sp, #8

    @ --- Step 7: call C context-switch handler ---
    bl      timer_irq_handler

    @ --- Step 8: restore SPSR_irq from (updated) saved_cpsr ---
    ldr     r0, =saved_cpsr
    ldr     r0, [r0]
    msr     spsr_cxsf, r0

    @ --- Step 9: load LR_irq from saved_lr (new process PC + 4) ---
    ldr     lr, =saved_lr
    ldr     lr, [lr]

    @ --- Step 10: restore R0-R12 from saved_regs ---
    @ ldmia reads the base address before any transfer, so r0 as base is safe.
    @ After the instruction r0 = saved_regs[0], r12 = saved_regs[12]; LR untouched.
    ldr     r0, =saved_regs
    ldmia   r0, {r0-r12}

    @ --- Step 11: return from IRQ (PC = LR-4, CPSR = SPSR) ---
    subs    pc, lr, #4
