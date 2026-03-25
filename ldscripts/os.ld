/*
 * os.ld - Linker script for the OS binary (BeagleBone Black / AM335x)
 *
 * Memory map:
 *   0x82000000  OS .text / .rodata / .data / .bss  (64 KB)
 *   0x82010000  OS stack region                     ( 8 KB, grows down from 0x82012000)
 *   0x82012000  IRQ-mode stack region               ( 8 KB, grows down from 0x82014000)
 *
 * SYS_STACK_TOP / IRQ_STACK_TOP are referenced by root.s for stack setup.
 */

SYS_STACK_TOP = 0x82012000;
IRQ_STACK_TOP = 0x82014000;

ENTRY(_start)

SECTIONS
{
    . = 0x82000000;

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
