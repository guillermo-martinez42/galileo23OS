# Bare-Metal ARM Multiprogramming OS for BeagleBone Black (AM335x, Cortex-A8)

## Project Overview

Implement a **minimal bare-metal multiprogramming (multitasking) system** on the BeagleBone Black (AM335x SoC, ARM Cortex-A8). The system runs **without any underlying OS** and manages three programs resident in memory:

1. **OS block** — Hardware initialization, interrupt handling, process scheduling, and context switching.
2. **User Process 1 (P1)** — Prints digits `0` through `9` in a loop, then repeats.
3. **User Process 2 (P2)** — Prints lowercase letters `a` through `z` in a loop, then repeats.

There is **no MMU** — all programs are loaded at **fixed memory addresses** defined at link time. The OS uses the AM335x **DMTimer2** to generate periodic interrupts (~1 second). On each timer interrupt, a **Round-Robin scheduler** performs a context switch between P1 and P2. The result is interleaved serial output (digits and letters alternating).

---

## Architecture (Layered)

- **Low-level OS**: `root.s` (assembly) + `os.c` — hardware init (UART, timer, INTC), vector table, IRQ handler, context switch, PCB management.
- **Library**: `stdio.c`, `string.c` — provide `PRINT()` (and optionally `READ()`) so user code never touches hardware directly. Include a minimal `sprintf`/`vsprintf`-like formatter supporting `%d`, `%c`, `%s`, `%x`, and `\n`.
- **User programs**: `P1/main.c` and `P2/main.c` — only printing logic, using the library `PRINT()` interface.

---

## Memory Map (Fixed, Non-Overlapping)

| Region       | Start Address | Size  | Description                    |
|--------------|---------------|-------|--------------------------------|
| OS code/data | 0x82000000    | 64 KB | OS .text, .data, .bss          |
| OS stack     | 0x82010000    | 8 KB  | OS stack (grows down)          |
| P1 code/data | 0x82100000    | 64 KB | Process 1 .text, .data, .bss   |
| P1 stack     | 0x82110000    | 8 KB  | Process 1 stack (grows down)   |
| P2 code/data | 0x82200000    | 64 KB | Process 2 .text, .data, .bss   |
| P2 stack     | 0x82210000    | 8 KB  | Process 2 stack (grows down)   |

---

## Detailed Implementation Tasks

### 1. `root.s` (Assembly Entry Point)

- Set CPU to **System mode** (or SVC mode).
- Set the **initial stack pointer** for the OS to top of OS stack region (e.g., `0x82012000`).
- **Clear the `.bss` section** (from `__bss_start__` to `__bss_end__`) to zero — required so global variables (PCB array, etc.) start in a known state.
- Place a **memory barrier** (`dsb; isb`) after clearing `.bss`.
- Set up the **ARM vector table** via **VBAR** (CP15 c12) so that the IRQ vector points to your IRQ handler.
- Jump to the C `main` function: `bl main`.

### 2. `os.c` — Hardware Initialization

#### Watchdog Timer (WDT1) — DISABLE FIRST
- Base: `0x44E35000`
- Write `0xAAAA` to `WDT_WSPR` (offset `0x48`), then wait until `WDT_WWPS` (offset `0x34`) bit 4 is clear.
- Write `0x5555` to `WDT_WSPR`, then wait until `WDT_WWPS` bit 4 is clear.
- **Must be the first thing in `main()`** or the board resets after ~60 seconds.

#### UART0 (Serial Output)
- Base: `0x44E09000`
- The bootloader (U-Boot) likely already initialized UART0. Your code must:
  - Use the correct base address.
  - **Poll the THR Empty (THRE) bit** — bit 5 of `LSR` (offset `0x14`) — before writing each character to `THR` (offset `0x00`).
- Implement `uart_putc(char c)` and `uart_puts(const char *s)`.

#### DMTimer2 (Periodic Interrupts)
- Base: `0x48040000`
- Key registers (offsets):
  - `TCLR` (0x38) — Timer control: set bits [1:0] = `0x3` for start + auto-reload.
  - `TIER` (0x2C) — Timer interrupt enable: set bit 1 (overflow interrupt enable = `0x2`).
  - `TISR` (0x28) — Timer interrupt status: write `0x2` to clear overflow flag.
  - `TLDR` (0x40) — Timer load register: set the reload value for ~1 second period.
  - `TCRR` (0x3C) — Timer counter: set initial value = TLDR value.
- For ~1 second at 24 MHz clock: `TLDR = 0xFE800000` (approx. `0xFFFFFFFF - 24000000 + 1`).
- Configuration sequence: set `TLDR`, set `TCRR = TLDR`, clear `TISR`, enable overflow in `TIER`, start timer via `TCLR = 0x3`.

#### INTC (Interrupt Controller)
- Base: `0x48200000`
- Key registers:
  - `INTC_SYSCONFIG` (0x10) — write `0x2` for soft reset, then wait for `INTC_SYSSTATUS` (0x14) bit 0.
  - `INTC_MIR_CLEAR2` (0xC8) — DMTimer2 is IRQ **68**. IRQ 68 is in bank 2 (IRQs 64–95), bit 4 (68 - 64 = 4). Write `(1 << 4)` to unmask it.
  - `INTC_CONTROL` (0x48) — write `0x1` (NEWIRQAGR) to signal End-of-Interrupt after handling.
  - `INTC_SIR_IRQ` (0x40) — read bits [6:0] to get the active IRQ number (should be 68).
- After INTC setup, **enable IRQs in the CPU** by clearing the I-bit in CPSR:
  ```c
  asm volatile("mrs r0, cpsr; bic r0, r0, #0x80; msr cpsr_c, r0" ::: "r0");
  ```

### 3. Vector Table and IRQ Handler (Assembly)

Implement the vector table and an IRQ handler in `root.s`:

```
@ Vector table (placed at a known address, loaded into VBAR)
_vector_table:
    ldr pc, =_reset_handler      @ Reset
    ldr pc, =_undef_handler      @ Undefined
    ldr pc, =_svc_handler        @ SVC
    ldr pc, =_prefetch_handler   @ Prefetch abort
    ldr pc, =_data_handler       @ Data abort
    nop                          @ Reserved
    ldr pc, =_irq_handler        @ IRQ
    ldr pc, =_fiq_handler        @ FIQ
```

**IRQ Handler** must:
1. Save `R0–R12` and `LR_irq` to global arrays (e.g., `saved_regs[13]` and `saved_lr`), accessible from C.
2. Call C function `timer_irq_handler()`.
3. After return, restore `R0–R12` and `LR_irq` from the (possibly updated) global arrays.
4. Return from interrupt: `subs pc, lr, #4`.

### 4. Process Control Block (PCB)

```c
typedef enum { READY, RUNNING } proc_state_t;

typedef struct {
    unsigned int pid;
    unsigned int regs[13];   // R0–R12
    unsigned int sp;         // R13 (SVC mode)
    unsigned int lr;         // R14 (SVC mode)
    unsigned int pc;         // Resume address
    unsigned int cpsr;       // Saved CPSR/SPSR
    proc_state_t state;
} pcb_t;

pcb_t pcb[3]; // pcb[0] = OS, pcb[1] = P1, pcb[2] = P2
int current_process = 1; // Start with P1
```

Initialize PCBs:
- `pcb[1].pc = 0x82100000` (P1 entry point), `pcb[1].sp = 0x82112000` (top of P1 stack), all regs = 0, `cpsr = 0x1F` (System mode, IRQs enabled).
- `pcb[2].pc = 0x82200000` (P2 entry point), `pcb[2].sp = 0x82212000` (top of P2 stack), all regs = 0, `cpsr = 0x1F`.

### 5. Context Switch Logic (`timer_irq_handler` in C)

```
void timer_irq_handler(void) {
    // 1. Clear timer interrupt: write 0x2 to TISR
    // 2. Send EOI: write 0x1 to INTC_CONTROL

    // 3. Save current process context from global saved_regs/saved_lr
    //    into pcb[current_process]
    //    PC = saved_lr - 4 (the interrupted instruction)
    //    Also save SVC-mode SP and LR by temporarily switching to SVC mode

    // 4. Round-robin: next = (current == 1) ? 2 : 1;
    //    Update states: pcb[current].state = READY; pcb[next].state = RUNNING;
    //    current_process = next;

    // 5. Restore next process context into global saved_regs/saved_lr
    //    Set saved_lr = pcb[next].pc + 4 (so subs pc, lr, #4 resumes correctly)
    //    Also restore SVC-mode SP and LR from PCB
}
```

**Critical detail for SVC mode SP/LR access**: The IRQ handler runs in IRQ mode, but the processes run in SVC mode. To save/restore SVC SP and LR:
```c
// Save SVC SP/LR (inside timer_irq_handler, IRQs already disabled in IRQ mode)
unsigned int svc_sp, svc_lr;
asm volatile(
    "mrs r0, cpsr\n"
    "bic r1, r0, #0x1F\n"
    "orr r1, r1, #0x13\n"    // SVC mode
    "orr r1, r1, #0x80\n"    // Keep IRQs disabled
    "msr cpsr_c, r1\n"
    "mov %0, sp\n"
    "mov %1, lr\n"
    "msr cpsr_c, r0\n"       // Back to IRQ mode
    : "=r"(svc_sp), "=r"(svc_lr) :: "r0", "r1"
);
```

### 6. Initial Stack Frame Setup

Before the first context switch to a user process, build an initial stack frame matching the restore path. A common layout is 14 words at the top of the stack (R0–R12 + LR), all zeros except LR = entry point address:

```c
void setup_initial_stack(pcb_t *p, unsigned int entry, unsigned int stack_top) {
    unsigned int *frame = (unsigned int *)(stack_top - 14 * 4);
    for (int i = 0; i < 13; i++) frame[i] = 0;  // R0–R12
    frame[13] = entry;  // LR = entry point
    p->sp = (unsigned int)frame;
    p->pc = entry;
    p->lr = entry;
    p->cpsr = 0x1F; // System mode, IRQs enabled
}
```

### 7. First Context Switch (OS → P1)

After all initialization, the OS performs the initial switch to P1. Load P1's context into the global arrays, set SVC SP/LR, then let the IRQ return path (or a dedicated jump) start P1. Alternatively, directly:
```c
// Set SVC mode SP and LR, then jump
asm volatile(
    "mov sp, %0\n"
    "mov lr, %1\n"
    "mov pc, lr\n"
    :: "r"(pcb[1].sp), "r"(pcb[1].pc)
);
```

### 8. User Processes

**P1/main.c:**
```c
extern void PRINT(const char *fmt, ...);

void main(void) {
    int n = 0;
    while (1) {
        PRINT("----From P1: %d\n", n);
        n = (n + 1) % 10;
        for (volatile int d = 0; d < 500000; d++); // delay
    }
}
```

**P2/main.c:**
```c
extern void PRINT(const char *fmt, ...);

void main(void) {
    char c = 'a';
    while (1) {
        PRINT("----From P2: %c\n", c);
        c = (c == 'z') ? 'a' : c + 1;
        for (volatile int d = 0; d < 500000; d++); // delay
    }
}
```

### 9. Library (`stdio.c` / `string.c`)

- Implement `PRINT(const char *fmt, ...)` using `<stdarg.h>` (va_list).
- Support format specifiers: `%d`, `%c`, `%s`, `%x`, `%%`.
- Internally call `uart_putc()` for each character.
- `string.c`: implement `strlen`, `strcpy`, `memset`, `memcpy` as needed.

### 10. Linker Scripts

**OS linker script (`os.ld`):**
```
ENTRY(_start)
SECTIONS {
    . = 0x82000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    __bss_start__ = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end__ = .;
}
```

**P1 linker script (`p1.ld`):**
```
ENTRY(main)
SECTIONS {
    . = 0x82100000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
```

**P2 linker script (`p2.ld`):** Same as P1 but `. = 0x82200000;`

### 11. Makefile

- Cross-compiler: `arm-none-eabi-gcc` and `arm-none-eabi-objcopy`
- Compiler flags: `-mcpu=cortex-a8 -marm -nostdlib -nostartfiles -ffreestanding -O1`
- Build the OS: compile `root.s`, `os.c`, `stdio.c`, `string.c` → link with `os.ld` → produce `os.bin` via objcopy.
- Build P1: compile `P1/main.c`, `stdio.c`, `string.c` → link with `p1.ld` → produce `p1.bin`.
- Build P2: compile `P2/main.c`, `stdio.c`, `string.c` → link with `p2.ld` → produce `p2.bin`.
- Add `clean` and `all` targets.

### 12. Loading Instructions (U-Boot)

Create a `README.md` or `Loads.txt` documenting:
```
loady 0x82000000   (send os.bin)
loady 0x82100000   (send p1.bin)
loady 0x82200000   (send p2.bin)
go 0x82000000
```

---

## Key AM335x Hardware Register Summary

| Peripheral | Base       | Key Offsets                                                    |
|------------|------------|----------------------------------------------------------------|
| UART0      | 0x44E09000 | THR=0x00, LSR=0x14 (bit5=THRE)                                |
| WDT1       | 0x44E35000 | WSPR=0x48, WWPS=0x34 (bit4=pending)                           |
| DMTimer2   | 0x48040000 | TCLR=0x38, TIER=0x2C, TISR=0x28, TLDR=0x40, TCRR=0x3C        |
| INTC       | 0x48200000 | SYSCONFIG=0x10, SYSSTATUS=0x14, SIR_IRQ=0x40, CONTROL=0x48, MIR_CLEAR2=0xC8 |

---

## Expected Output (Serial Console)

```
----From P1: 0
----From P1: 1
----From P1: 2
----From P2: a
----From P2: b
----From P2: c
----From P1: 3
----From P1: 4
----From P2: d
...
```

Both processes produce output over time, demonstrating round-robin scheduling.

---

## Implementation Hints

- **Return address**: On IRQ, ARM sets `LR_irq = PC + 4` (or +8 depending on pipeline). Save `PC = LR_irq - 4` as the interrupted instruction. When restoring, set `LR_irq = saved_PC + 4` so `subs pc, lr, #4` returns correctly.
- **Memory barriers**: After initializing PCBs in C, insert `asm volatile("dsb" ::: "memory");` before the first context switch.
- **Disable interrupts** during the critical section of context switch (mode switching to read/write SVC SP/LR).
- **Timer value**: For ~1s at 24 MHz, use `TLDR = 0xFE800000`.
- **INTC EOI**: Always write `0x1` to `INTC_CONTROL` (offset 0x48) after handling the IRQ, otherwise no further interrupts will be delivered.
- **No dynamic loading**: P1 and P2 binaries are pre-loaded at their fixed addresses by U-Boot before the OS starts.

---

## File Structure to Create

```
project/
├── Makefile
├── README.md          (loading instructions)
├── os.ld              (OS linker script)
├── p1.ld              (P1 linker script)
├── p2.ld              (P2 linker script)
├── root.s             (assembly: entry, vector table, IRQ handler)
├── os.c               (hardware init, PCB, context switch, scheduler)
├── os.h               (PCB struct, externs for shared globals)
├── stdio.c            (PRINT implementation)
├── stdio.h            (PRINT prototype)
├── string.c           (strlen, memset, etc.)
├── string.h           (string function prototypes)
├── P1/
│   └── main.c         (digit printing process)
└── P2/
    └── main.c         (letter printing process)
```

Please create ALL files with complete, working implementations. The code must compile with `arm-none-eabi-gcc` and produce three separate binaries (`os.bin`, `p1.bin`, `p2.bin`) ready to load on a BeagleBone Black via U-Boot.
