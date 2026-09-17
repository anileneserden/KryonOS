.set ALIGN,    1<<0             # alignment flag
.set MEMINFO,  1<<1             # memory information flag
.set GRAPHICS, 1<<2             # graphics mode request (Multiboot v1 framebuffer)
.set FLAGS,    ALIGN | MEMINFO | GRAPHICS  # Multiboot flags
.set MAGIC,    0x1BADB002       # Multiboot magic number
.set CHECKSUM, -(MAGIC + FLAGS) # checksum

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
# Multiboot v1 graphics request fields (linear framebuffer parameters)
.long 0                         # header_addr (0 is recommended)
.long 0                         # load_addr
.long 0                         # load_end_addr
.long 0                         # bss_end_addr
.long 0                         # entry_addr
.long 0                         # mode_type (0 = linear graphics mode)
.long 1920                      # width
.long 1080                      # height
.long 32                        # depth (bits per pixel - 32-bit color)

.section .text
.global _start
.type _start, @function
_start:
    # Set up the stack
    mov $stack_top, %esp

    # Pass the Multiboot information address (ebx) and magic number (eax) as arguments to the C function
    push %ebx
    push %eax

    # Call the C main function
    call kernel_main

    # Enter an infinite loop if main returns
.Lhang:
    cli
    hlt
    jmp .Lhang

.size _start, . - _start

# Reserve space for the stack
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KB Stack
stack_top: