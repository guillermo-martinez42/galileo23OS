/*
 * os_qemu.ld - Linker script for the OS binary (QEMU -M virt)
 *
 * Phase 2 memory map (banked stacks for every privileged mode):
 *   0x40000000  OS .text / .rodata / .data / .bss  (64 KB)
 *   0x40010000  SYS-mode stack region              ( 8 KB, top 0x40012000)
 *   0x40012000  IRQ-mode stack region              ( 8 KB, top 0x40014000)
 *   0x40014000  SVC-mode stack region              ( 8 KB, top 0x40016000)
 *   0x40016000  ABT-mode stack region              ( 4 KB, top 0x40017000)
 *   0x40017000  UND-mode stack region              ( 4 KB, top 0x40018000)
 *
 * RAM on QEMU virt starts at 0x40000000 (128 MB by default).
 * Stack-top symbols are referenced by boot/qemu_startup.s.
 */

SYS_STACK_TOP = 0x40012000;
IRQ_STACK_TOP = 0x40014000;
SVC_STACK_TOP = 0x40016000;
ABT_STACK_TOP = 0x40017000;
UND_STACK_TOP = 0x40018000;

ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    __bss_start__ = .;
    .bss :
    {
        *(.bss*)
        *(COMMON)
    }
    __bss_end__ = .;
}
