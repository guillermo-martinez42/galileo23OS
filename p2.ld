/*
 * p2.ld - Linker script for Process 2
 *
 * Memory map:
 *   0x82200000  P2 .text / .rodata / .data / .bss  (64 KB)
 *   0x82210000  P2 stack region                     ( 8 KB, grows down from 0x82212000)
 */

ENTRY(main)

SECTIONS
{
    . = 0x82200000;

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
