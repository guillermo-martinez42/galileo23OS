# BeagleBone Black Bare-Metal Multiprogramming OS

Minimal bare-metal OS for the AM335x (Cortex-A8) implementing round-robin
multitasking between two user processes without an MMU or host OS.

---

## Building

```
make
```

Requires `arm-none-eabi-gcc` and `arm-none-eabi-objcopy` on your PATH.
Produces three flat binary images: `os.bin`, `p1.bin`, `p2.bin`.

---

## Loading onto the BeagleBone Black via U-Boot

Connect a 3.3 V serial cable to the BBB debug UART (J1 header, 115200 8N1).
Stop U-Boot at the prompt by pressing any key during boot.

Load each binary at its fixed address using Y-Modem (`loady`):

```
=> loadx 0x82000000
```
*(Send `os.bin` from your terminal's Y-Modem upload dialog)*

```
=> loadx 0x82100000
```
*(Send `p1.bin`)*

```
=> loadx 0x82200000
```
*(Send `p2.bin`)*

Start execution:

```
=> go 0x82000000
```

---

## Memory Map

| Region       | Start        | Size  | Description                             |
|--------------|--------------|-------|-----------------------------------------|
| OS code/data | `0x82000000` | 64 KB | OS `.text`, `.rodata`, `.data`, `.bss`  |
| OS stack     | `0x82010000` | 8 KB  | System-mode stack (top = `0x82012000`)  |
| IRQ stack    | `0x82012000` | 8 KB  | IRQ-mode stack   (top = `0x82014000`)   |
| P1 code/data | `0x82100000` | 64 KB | Process 1 binary                        |
| P1 stack     | `0x82110000` | 8 KB  | P1 stack         (top = `0x82112000`)   |
| P2 code/data | `0x82200000` | 64 KB | Process 2 binary                        |
| P2 stack     | `0x82210000` | 8 KB  | P2 stack         (top = `0x82212000`)   |

---

## Expected Serial Output

The two processes are interleaved by a ~1-second DMTimer2 interrupt:

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

---

## Architecture Summary

| File       | Role                                                              |
|------------|-------------------------------------------------------------------|
| `root.s`   | Entry point, CPU mode setup, BSS clear, VBAR, vector table, IRQ handler |
| `os.c`     | WDT disable, timer init, INTC init, PCBs, `timer_irq_handler()`  |
| `os.h`     | `pcb_t` struct, extern declarations for shared globals            |
| `stdio.c`  | `uart_putc`, `uart_puts`, `PRINT` (compiled into all 3 binaries)  |
| `stdio.h`  | `PRINT` prototype                                                 |
| `string.c` | `strlen`, `strcpy`, `memset`, `memcpy`                            |
| `string.h` | String function prototypes                                        |
| `P1/main.c`| User process 1 — digits                                           |
| `P2/main.c`| User process 2 — letters                                          |
| `os.ld`    | Linker script — OS at `0x82000000`                                |
| `p1.ld`    | Linker script — P1 at `0x82100000`                                |
| `p2.ld`    | Linker script — P2 at `0x82200000`                                |

---

## Project Structure

This project follows the **Linux-Lite Structure**, a subsystem-based source layout modelled after the Linux kernel's directory conventions. Rather than grouping all code in a flat root directory, each concern lives in its own folder: `boot/` for startup assembly, `core/` for the scheduler and process management, `drivers/` for hardware-specific peripheral code organised by platform, `lib/` for shared utilities, `usr/` for unprivileged user processes, `ldscripts/` for linker scripts, and `include/` for all shared headers. This separation ensures that hardware knowledge never leaks into the scheduler, that user-process code cannot accidentally reference kernel internals, and that adding a new target platform only requires adding a new subdirectory under `drivers/`.
