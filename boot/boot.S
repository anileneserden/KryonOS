.set ALIGN,    1<<0             # hizalama bayrağı
.set MEMINFO,  1<<1             # bellek bilgisi bayrağı
.set FLAGS,    ALIGN | MEMINFO  # multiboot bayrakları
.set MAGIC,    0x1BADB002       # multiboot sihirli sayı (magic number)
.set CHECKSUM, -(MAGIC + FLAGS) # sağlama toplamı

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .text
.global _start
.type _start, @function
_start:
    # Stack (yığın) alanını ayarla
    mov $stack_top, %esp

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