.set ALIGN,    1<<0             # hizalama bayrağı
.set MEMINFO,  1<<1             # bellek bilgisi bayrağı
.set GRAPHICS, 1<<2             # grafik modu isteği (multiboot v1 framebuffer)
.set FLAGS,    ALIGN | MEMINFO | GRAPHICS  # multiboot bayrakları
.set MAGIC,    0x1BADB002       # multiboot sihirli sayı (magic number)
.set CHECKSUM, -(MAGIC + FLAGS) # sağlama toplamı

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
# Multiboot v1 grafik istek alanları (linear framebuffer parametreleri)
.long 0                         # header_addr (0 kullanılması tavsiye edilir)
.long 0                         # load_addr
.long 0                         # load_end_addr
.long 0                         # bss_end_addr
.long 0                         # entry_addr
.long 0                         # mode_type (0 = linear graphics mode)
.long 1920                      # width (Genişlik)
.long 1080                      # height (Yükseklik)
.long 32                        # depth (Bit per pixel - 32 bit renk)

.section .text
.global _start
.type _start, @function
_start:
    # Stack (yığın) alanını ayarla
    mov $stack_top, %esp

    # Multiboot bilgi yapısının adresini (ebx) ve magic number'ı (eax) C fonksiyonuna argüman olarak aktar
    push %ebx
    push %eax

    # C dilindeki main fonksiyonunu çağır
    call kernel_main

    # Eğer main dönerse sistemi sonsuz döngüye sok
.Lhang:
    cli
    hlt
    jmp .Lhang

.size _start, . - _start

# Stack alanı için yer ayırma
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KB Stack
stack_top: