CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-ld

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
LDFLAGS = -T linker.ld -nostdlib

BUILD = build
TARGET = $(BUILD)/kryonos.bin

# --- Kaynak Dosyalar ---
SRC_S = boot/boot.S
SRC_C = \
	kernel/kmain.c \
	kernel/serial.c

# Kaynak yollarını build/ altındaki nesne dosyalarına (object) dönüştür
OBJS = $(SRC_S:%.S=$(BUILD)/%.o) \
		$(SRC_C:%.c=$(BUILD)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS)

# C dosyaları için hiyerarşik derleme kuralı
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly dosyaları için hiyerarşik derleme kuralı
$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $< -o $@

clean:
	rm -rf $(BUILD) isodir kryonos.iso

iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/kryonos.bin
	echo 'set gfxpayload=800x600x32' > isodir/boot/grub/grub.cfg
	echo 'menuentry "KryonOS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/kryonos.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o kryonos.iso isodir
	rm -rf isodir

run: iso
	qemu-system-i386 -vga std -cdrom kryonos.iso -serial stdio