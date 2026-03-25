/*
 * os_qemu.ld - Linker script for the OS binary (QEMU -M virt)
 *
 * Memory map:
 *   0x40000000  OS .text / .rodata / .data / .bss  (64 KB)
 *   0x40010000  OS stack region                     ( 8 KB, grows down from 0x40012000)
 *   0x40012000  IRQ-mode stack region               ( 8 KB, grows down from 0x40014000)
 *
 * RAM on QEMU virt starts at 0x40000000 (128 MB by default).
 * SYS_STACK_TOP / IRQ_STACK_TOP are referenced by root.s for stack setup.
 */

SYS_STACK_TOP = 0x40012000;
IRQ_STACK_TOP = 0x40014000;

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
