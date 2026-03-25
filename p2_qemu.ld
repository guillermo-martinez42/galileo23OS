/*
 * p2_qemu.ld - Linker script for Process 2 (QEMU -M virt)
 *
 * Memory map:
 *   0x40200000  P2 .text / .rodata / .data / .bss  (64 KB)
 *   0x40210000  P2 stack region                     ( 8 KB, grows down from 0x40212000)
 */

ENTRY(main)

SECTIONS
{
    . = 0x40200000;

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
