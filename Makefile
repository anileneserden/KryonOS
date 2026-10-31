CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-ld

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -Iinclude/libc
LDFLAGS = -T linker.ld -nostdlib

BUILD = build
TARGET = $(BUILD)/lenoros.elf

# --- Disk Image Definitions ---
DISK_LNRFS ?= $(HOME)/LenorOS/main/disk-lnrfs.img
DISK_FAT32 ?= $(HOME)/LenorOS/main/disk-fat32.img

# --- Source Files ---
SRC_S = \
    boot/boot.S \
    boot/paging.S

SRC_C = \
    kernel/audio/wav.c \
    kernel/crypto/tls.c \
    kernel/app_manager.c \
    kernel/hexdump.c \
    kernel/kmain.c \
    kernel/power.c \
    kernel/interrupts.c \
    kernel/serial.c \
    kernel/string.c \
    kernel/drivers/audio/ac97.c \
    kernel/drivers/audio/pcspeaker.c \
    kernel/drivers/input/keyboard_ps2.c \
    kernel/drivers/input/mouse_ps2.c \
    kernel/drivers/input/mouse_usb.c \
    kernel/drivers/net/e1000.c \
    kernel/drivers/storage/ata.c \
    kernel/drivers/usb/ehci.c \
    kernel/drivers/usb/uhci.c \
    kernel/drivers/usb/xhci.c \
    kernel/drivers/video/font/font8x8_basic.c \
    kernel/drivers/video/font/font8x16_basic.c \
    kernel/drivers/video/fb.c \
    kernel/drivers/video/gfx.c \
    kernel/drivers/pci.c \
    kernel/drivers/rtc.c \
    kernel/fs/fat32.c \
    kernel/fs/lnrfs.c \
    kernel/fs/vfs.c \
    kernel/loader/lex.c \
    kernel/loader/lex2.c \
    kernel/loader/png.c \
    kernel/mem/heap.c \
    kernel/mem/pmm.c \
    kernel/mem/vmm.c \
    libc/math.c \
    libc/stdlib.c \
    ui/camera.c \
    ui/cursor.c \
    ui/desktop_icons.c \
    ui/desktop.c \
    ui/grid.c \
    ui/renderer3d.c \
    ui/serial_screen.c \
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
	rm -rf $(BUILD) isodir lenoros.iso

iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/lenoros.bin
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'insmod all_video' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "LenorOS UEFI/BIOS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/lenoros.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o lenoros.iso isodir

# Dosyalar yoksa otomatik boş imaj oluşturan kural
$(DISK_LNRFS):
	@mkdir -p $(dir $@)
	dd if=/dev/zero of=$@ bs=1M count=2

$(DISK_FAT32):
	@mkdir -p $(dir $@)
	dd if=/dev/zero of=$@ bs=1M count=16

# QEMU'yu UEFI modunda çalıştırmak için -bios ekledik
run: iso $(DISK_LNRFS) $(DISK_FAT32)
	qemu-system-x86_64 \
       -bios /usr/share/edk2/x64/OVMF.4m.fd \
       -cdrom lenoros.iso \
       -m 2G \
       -drive format=raw,file=$(DISK_LNRFS),index=0,media=disk \
       -drive format=raw,file=$(DISK_FAT32),index=1,media=disk \
       -audiodev pa,id=audio0 -device AC97,audiodev=audio0 \
       -machine pcspk-audiodev=audio0 \
       -device ich9-usb-ehci1,id=ehci,addr=1d.7,multifunction=on \
       -device ich9-usb-uhci1,id=uhci.0,addr=1d.0,multifunction=on,masterbus=ehci.0,firstport=0 \
       -device ich9-usb-uhci2,id=uhci.1,addr=1d.1,multifunction=on,masterbus=ehci.0,firstport=2 \
       -device ich9-usb-uhci3,id=uhci.2,addr=1d.2,multifunction=on,masterbus=ehci.0,firstport=4 \
       -device usb-tablet,bus=ehci.0,port=1 \
       -netdev user,id=net0 -device e1000,netdev=net0 \
       -rtc base=localtime \
       -serial stdio -vga std -display gtk,gl=on
