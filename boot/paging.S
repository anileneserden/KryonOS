.global load_page_directory
.global enable_paging

load_page_directory:
    push %ebp
    mov %esp, %ebp
    mov 8(%esp), %eax
    mov %eax, %cr3
    mov %ebp, %esp
    pop %ebp
    ret

enable_paging:
    push %ebp
    mov %esp, %ebp
    mov %cr0, %eax
    or $0x80000000, %eax   # Set PG bit in CR0 to enable paging
    mov %eax, %cr0
    pop %ebp
    ret