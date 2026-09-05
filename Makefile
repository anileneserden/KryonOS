CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-ld

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T linker.ld -nostdlib

OBJS = boot.o main.o
TARGET = kryonos.bin

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS)

%.o: %.S
	$(AS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) kryonos.iso
	rm -rf isodir

iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/kryonos.bin
	echo 'menuentry "KryonOS" {' > isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/kryonos.bin' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o kryonos.iso isodir
	rm -rf isodir

run: iso
	qemu-system-i386 -cdrom kryonos.iso