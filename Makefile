# Galileo23OS Makefile — Subsystem Layout
#
# Toolchain: arm-none-eabi-gcc  (ARM bare-metal cross-compiler)
#
# Targets:
#   make            — BeagleBone Black: bin/bbb/os.bin  p1.bin  p2.bin
#   make qemu       — QEMU virt:        bin/virt/os_qemu.elf  p1_qemu.elf  p2_qemu.elf
#   make run-qemu   — Build QEMU images and launch QEMU
#   make debug-qemu — Build + launch QEMU paused with GDB server on :1234
#   make clean      — Wipe bin/ directory and any stray .o files

CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy

# ---------------------------------------------------------------
# Directory layout
# ---------------------------------------------------------------
BOOT_DIR := boot
CORE_DIR := core
DRV_BBB  := drivers/beagle
DRV_QEMU := drivers/qemu
LIB_DIR  := lib
USR_DIR  := usr
LD_DIR   := ldscripts
BIN_BBB  := bin/bbb
BIN_VIRT := bin/virt

# ---------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------

# Centralised headers — every .c and .s file finds its #includes here
INCLUDES := -Iinclude

# BeagleBone Black — AM335x Cortex-A8, no debug symbols
# -mcpu=cortex-a8  : target the AM335x Cortex-A8
# -marm            : force ARM (not Thumb) instruction set
# -nostdlib        : no standard library linkage
# -nostartfiles    : no crt0.o / hosted startup
# -ffreestanding   : no hosted-environment assumptions
# -O1              : light optimisation (avoids aggressive reordering)
CFLAGS := -mcpu=cortex-a8 -marm -nostdlib -nostartfiles \
          -ffreestanding -O1 -Wall -Wextra $(INCLUDES)

# QEMU virt — Cortex-A15, full debug symbols, QEMU hardware paths
# -mcpu=cortex-a15 : QEMU -M virt works best with A15 (GICv2 + Generic Timer)
# -g               : include DWARF debug info for GDB
# -DQEMU           : activates QEMU code paths in all source files
QEMU_CFLAGS := -mcpu=cortex-a15 -marm -nostdlib -nostartfiles \
               -ffreestanding -O1 -Wall -Wextra -g -DQEMU $(INCLUDES)

# ---------------------------------------------------------------
# Source file groups
# ---------------------------------------------------------------

# Shared library (compiled into every binary — OS and user processes)
LIB_SRCS := $(LIB_DIR)/stdio.c \
            $(LIB_DIR)/string.c

# Platform-independent kernel core
CORE_SRCS := $(CORE_DIR)/sched.c \
             $(CORE_DIR)/pcb.c

# OS kernel source sets — startup + core + platform driver + lib
BBB_OS_SRCS := $(BOOT_DIR)/beagle_startup.s \
               $(CORE_SRCS)                  \
               $(DRV_BBB)/am335x_timer.c     \
               $(LIB_SRCS)

VIRT_OS_SRCS := $(BOOT_DIR)/qemu_startup.s \
                $(CORE_SRCS)                \
                $(DRV_QEMU)/virt_timer.c    \
                $(LIB_SRCS)

# User-process source sets (kernel and driver code excluded — no privilege)
P1_SRCS := $(USR_DIR)/P1/main.c $(LIB_SRCS)
P2_SRCS := $(USR_DIR)/P2/main.c $(LIB_SRCS)

# ---------------------------------------------------------------
# Phony targets
# ---------------------------------------------------------------
.PHONY: all qemu run-qemu debug-qemu clean

# ---------------------------------------------------------------
# BeagleBone Black — raw binary images for U-Boot loady
# ---------------------------------------------------------------
all: $(BIN_BBB)/os.bin $(BIN_BBB)/p1.bin $(BIN_BBB)/p2.bin

# Create output directory on demand (order-only prerequisite)
$(BIN_BBB):
	mkdir -p $@

$(BIN_BBB)/os.bin: $(BBB_OS_SRCS) $(LD_DIR)/os.ld | $(BIN_BBB)
	$(CC) $(CFLAGS) -T $(LD_DIR)/os.ld \
	      -o $(BIN_BBB)/os.elf $(BBB_OS_SRCS)
	$(OBJCOPY) -O binary $(BIN_BBB)/os.elf $@

$(BIN_BBB)/p1.bin: $(P1_SRCS) $(LD_DIR)/p1.ld | $(BIN_BBB)
	$(CC) $(CFLAGS) -T $(LD_DIR)/p1.ld \
	      -o $(BIN_BBB)/p1.elf $(P1_SRCS)
	$(OBJCOPY) -O binary $(BIN_BBB)/p1.elf $@

$(BIN_BBB)/p2.bin: $(P2_SRCS) $(LD_DIR)/p2.ld | $(BIN_BBB)
	$(CC) $(CFLAGS) -T $(LD_DIR)/p2.ld \
	      -o $(BIN_BBB)/p2.elf $(P2_SRCS)
	$(OBJCOPY) -O binary $(BIN_BBB)/p2.elf $@

# ---------------------------------------------------------------
# QEMU virt — ELF images with debug symbols
# ---------------------------------------------------------------
qemu: $(BIN_VIRT)/os_qemu.elf $(BIN_VIRT)/p1_qemu.elf $(BIN_VIRT)/p2_qemu.elf

$(BIN_VIRT):
	mkdir -p $@

$(BIN_VIRT)/os_qemu.elf: $(VIRT_OS_SRCS) $(LD_DIR)/os_qemu.ld | $(BIN_VIRT)
	$(CC) $(QEMU_CFLAGS) -T $(LD_DIR)/os_qemu.ld \
	      -o $@ $(VIRT_OS_SRCS)

$(BIN_VIRT)/p1_qemu.elf: $(P1_SRCS) $(LD_DIR)/p1_qemu.ld | $(BIN_VIRT)
	$(CC) $(QEMU_CFLAGS) -T $(LD_DIR)/p1_qemu.ld \
	      -o $@ $(P1_SRCS)

$(BIN_VIRT)/p2_qemu.elf: $(P2_SRCS) $(LD_DIR)/p2_qemu.ld | $(BIN_VIRT)
	$(CC) $(QEMU_CFLAGS) -T $(LD_DIR)/p2_qemu.ld \
	      -o $@ $(P2_SRCS)

# ---------------------------------------------------------------
# Run QEMU
#
# -M virt              : generic ARM virtual board
# -cpu cortex-a15      : Cortex-A15 (GICv2 + Generic Timer)
# -m 128M              : 128 MB RAM starting at 0x40000000
# -display none        : no graphical window
# -serial stdio        : UART output to this terminal
# -kernel ...          : load OS ELF, entry point at _start
# -device loader,...   : load P1 and P2 at their fixed addresses
# ---------------------------------------------------------------
run-qemu: $(BIN_VIRT)/os_qemu.elf $(BIN_VIRT)/p1_qemu.elf $(BIN_VIRT)/p2_qemu.elf
	qemu-system-arm \
	  -M virt \
	  -cpu cortex-a15 \
	  -m 128M \
	  -display none \
	  -serial stdio \
	  -kernel $(BIN_VIRT)/os_qemu.elf \
	  -device loader,file=$(BIN_VIRT)/p1_qemu.elf \
	  -device loader,file=$(BIN_VIRT)/p2_qemu.elf

# ---------------------------------------------------------------
# Debug with GDB
#
# Starts QEMU paused, listening for GDB on localhost:1234.
# Connect with:
#   arm-none-eabi-gdb bin/virt/os_qemu.elf
#   (gdb) target remote :1234
#   (gdb) continue
# ---------------------------------------------------------------
debug-qemu: $(BIN_VIRT)/os_qemu.elf $(BIN_VIRT)/p1_qemu.elf $(BIN_VIRT)/p2_qemu.elf
	qemu-system-arm \
	  -M virt \
	  -cpu cortex-a15 \
	  -m 128M \
	  -display none \
	  -serial stdio \
	  -kernel $(BIN_VIRT)/os_qemu.elf \
	  -device loader,file=$(BIN_VIRT)/p1_qemu.elf \
	  -device loader,file=$(BIN_VIRT)/p2_qemu.elf \
	  -S -gdb tcp::1234

# ---------------------------------------------------------------
# Clean — wipe all build output
# ---------------------------------------------------------------
clean:
	rm -rf bin/
	find . -name "*.o" -delete
