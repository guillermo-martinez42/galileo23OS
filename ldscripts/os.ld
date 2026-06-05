/*
 * os.ld - Linker script for the OS binary (BeagleBone Black / AM335x)
 *
 * Phase 2 memory map (banked stacks for every privileged mode):
 *   0x82000000  OS .text / .rodata / .data / .bss  (64 KB)
 *   0x82010000  SYS-mode stack region              ( 8 KB, top 0x82012000)
 *   0x82012000  IRQ-mode stack region              ( 8 KB, top 0x82014000)
 *   0x82014000  SVC-mode stack region              ( 8 KB, top 0x82016000)
 *   0x82016000  ABT-mode stack region              ( 4 KB, top 0x82017000)
 *   0x82017000  UND-mode stack region              ( 4 KB, top 0x82018000)
 *
 * Stack-top symbols are referenced by boot/beagle_startup.s.
 */

SYS_STACK_TOP = 0x82012000;
IRQ_STACK_TOP = 0x82014000;
SVC_STACK_TOP = 0x82016000;
ABT_STACK_TOP = 0x82017000;
UND_STACK_TOP = 0x82018000;

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
