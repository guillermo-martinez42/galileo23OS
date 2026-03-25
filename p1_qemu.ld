/*
 * p1_qemu.ld - Linker script for Process 1 (QEMU -M virt)
 *
 * Memory map:
 *   0x40100000  P1 .text / .rodata / .data / .bss  (64 KB)
 *   0x40110000  P1 stack region                     ( 8 KB, grows down from 0x40112000)
 */

ENTRY(main)

SECTIONS
{
    . = 0x40100000;

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
