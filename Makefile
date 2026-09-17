CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-ld

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
LDFLAGS = -T linker.ld -nostdlib

BUILD = build
TARGET = $(BUILD)/kryonos.bin

# --- Disk Image Definitions ---
DISK_KRYFS ?= $(HOME)/KryonOS/main/disk-kryfs.img
DISK_FAT32 ?= $(HOME)/KryonOS/main/disk-fat32.img

# --- Source Files ---
SRC_S = \
    boot/boot.S \
    boot/paging.S

SRC_C = \
    kernel/audio/wav.c \
    kernel/app_manager.c \
    kernel/kef_loader.c \
    kernel/kmain.c \
    kernel/serial.c \
    kernel/string.c \
    kernel/drivers/audio/ac97.c \
    kernel/drivers/input/keyboard_ps2.c \
    kernel/drivers/input/mouse_ps2.c \
    kernel/drivers/storage/ata.c \
    kernel/drivers/video/font/font8x8_basic.c \
    kernel/drivers/video/font/font8x16_basic.c \
    kernel/drivers/video/fb.c \
    kernel/drivers/video/gfx.c \
    kernel/drivers/pci.c \
    kernel/fs/fat32.c \
    kernel/fs/kryfs.c \
    kernel/fs/vfs.c \
    kernel/mem/heap.c \
    kernel/mem/pmm.c \
    kernel/mem/vmm.c \
    ui/cursor.c \
    ui/desktop_icons.c \
    ui/desktop.c \
    ui/grid.c \
    ui/window.c \
    ui/wm.c

# Convert source paths to object files under build/
OBJS = $(SRC_S:%.S=$(BUILD)/%.o) \
        $(SRC_C:%.c=$(BUILD)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS)

# Hierarchical build rule for C files
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Hierarchical build rule for assembly files
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $< -o $@

clean:
	rm -rf $(BUILD) isodir kryonos.iso

iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/kryonos.bin
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set gfxpayload=1920x1080x32' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "KryonOS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/kryonos.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o kryonos.iso isodir

run: iso
	qemu-system-i386 -cdrom kryonos.iso \
		-drive format=raw,file=$(DISK_KRYFS),index=0,media=disk \
		-drive format=raw,file=$(DISK_FAT32),index=1,media=disk \
		-audiodev pa,id=audio0 -device AC97,audiodev=audio0 \
		-serial stdio -vga std -display sdl,gl=on