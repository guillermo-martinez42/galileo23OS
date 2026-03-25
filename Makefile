# Makefile - Bare-Metal Multiprogramming OS
#
# Toolchain: arm-none-eabi-gcc (ARM bare-metal cross-compiler)
#
# Targets:
#   make           — BeagleBone Black: produces os.bin  p1.bin  p2.bin
#   make qemu      — QEMU virt:        produces os_qemu.elf  p1_qemu.elf  p2_qemu.elf
#   make run-qemu  — Build QEMU images and launch QEMU
#   make debug-qemu— Launch QEMU with GDB server (port 1234, paused at boot)
#   make clean     — Remove all generated files

CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy

# ---------------------------------------------------------------
# BeagleBone Black flags (Cortex-A8, no debug symbols)
# -mcpu=cortex-a8  : target the AM335x Cortex-A8
# -marm            : force ARM (not Thumb) instruction set
# -nostdlib        : no standard library
# -nostartfiles    : no crt0.o / standard startup files
# -ffreestanding   : no hosted-environment assumptions
# -O1              : light optimisation (avoids over-aggressive reordering)
# ---------------------------------------------------------------
CFLAGS := -mcpu=cortex-a8 -marm -nostdlib -nostartfiles -ffreestanding -O1 -Wall -Wextra

# ---------------------------------------------------------------
# QEMU virt flags (Cortex-A15, with debug symbols)
# -mcpu=cortex-a15 : QEMU -M virt works best with A15 or A7
# -g               : include DWARF debug info for GDB
# -DQEMU           : selects QEMU paths in os.c and stdio.c
# ---------------------------------------------------------------
QEMU_CFLAGS := -mcpu=cortex-a15 -marm -nostdlib -nostartfiles \
               -ffreestanding -O1 -Wall -Wextra -g -DQEMU

.PHONY: all qemu run-qemu debug-qemu clean

# ---------------------------------------------------------------
# Default: BeagleBone Black binaries (ready for U-Boot loady)
# ---------------------------------------------------------------
all: os.bin p1.bin p2.bin

os.bin: root.s os.c stdio.c string.c os.ld
	$(CC) $(CFLAGS) -T os.ld -o os.elf root.s os.c stdio.c string.c
	$(OBJCOPY) -O binary os.elf os.bin

p1.bin: P1/main.c stdio.c string.c p1.ld
	$(CC) $(CFLAGS) -T p1.ld -o p1.elf P1/main.c stdio.c string.c
	$(OBJCOPY) -O binary p1.elf p1.bin

p2.bin: P2/main.c stdio.c string.c p2.ld
	$(CC) $(CFLAGS) -T p2.ld -o p2.elf P2/main.c stdio.c string.c
	$(OBJCOPY) -O binary p2.elf p2.bin

# ---------------------------------------------------------------
# QEMU: ELF images with debug symbols
#
# os_qemu.elf  — OS kernel (loaded via -kernel)
# p1_qemu.elf  — Process 1 (loaded via -device loader at 0x40100000)
# p2_qemu.elf  — Process 2 (loaded via -device loader at 0x40200000)
# ---------------------------------------------------------------
qemu: os_qemu.elf p1_qemu.elf p2_qemu.elf

os_qemu.elf: root.s os.c stdio.c string.c os_qemu.ld
	$(CC) $(QEMU_CFLAGS) -T os_qemu.ld -o os_qemu.elf \
	      root.s os.c stdio.c string.c

p1_qemu.elf: P1/main.c stdio.c string.c p1_qemu.ld
	$(CC) $(QEMU_CFLAGS) -T p1_qemu.ld -o p1_qemu.elf \
	      P1/main.c stdio.c string.c

p2_qemu.elf: P2/main.c stdio.c string.c p2_qemu.ld
	$(CC) $(QEMU_CFLAGS) -T p2_qemu.ld -o p2_qemu.elf \
	      P2/main.c stdio.c string.c

# ---------------------------------------------------------------
# Run QEMU
#
# -M virt              : generic ARM virtual board
# -cpu cortex-a15      : Cortex-A15 (GICv2 + Generic Timer compatible)
# -m 128M              : 128 MB RAM at 0x40000000
# -display none        : no graphical window
# -serial stdio        : UART output to terminal
# -kernel os_qemu.elf  : load OS ELF and start at _start
# -device loader,...   : load P1 and P2 into their fixed addresses
# ---------------------------------------------------------------
run-qemu: os_qemu.elf p1_qemu.elf p2_qemu.elf
	qemu-system-arm \
	  -M virt \
	  -cpu cortex-a15 \
	  -m 128M \
	  -display none \
	  -serial stdio \
	  -kernel os_qemu.elf \
	  -device loader,file=p1_qemu.elf \
	  -device loader,file=p2_qemu.elf

# ---------------------------------------------------------------
# Debug with GDB
#
# Starts QEMU paused, listening for GDB on localhost:1234.
# Connect with:
#   arm-none-eabi-gdb os_qemu.elf
#   (gdb) target remote :1234
#   (gdb) continue
# ---------------------------------------------------------------
debug-qemu: os_qemu.elf p1_qemu.elf p2_qemu.elf
	qemu-system-arm \
	  -M virt \
	  -cpu cortex-a15 \
	  -m 128M \
	  -display none \
	  -serial stdio \
	  -kernel os_qemu.elf \
	  -device loader,file=p1_qemu.elf \
	  -device loader,file=p2_qemu.elf \
	  -S -gdb tcp::1234

# ---------------------------------------------------------------
# Clean
# ---------------------------------------------------------------
clean:
	rm -f *.elf *.bin
