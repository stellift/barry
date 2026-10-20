.set MULTIBOOT2_HEADER_MAGIC, 0xe85250d6
.set MULTIBOOT2_ARCHITECTURE_I386, 0

.set PAGE_PRESENT, 0x001
.set PAGE_WRITABLE, 0x002
.set PAGE_LARGE, 0x080
.set TABLE_FLAGS, PAGE_PRESENT | PAGE_WRITABLE
.set LARGE_LEAF_FLAGS, PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE

.set CR4_PAE, 0x020
.set CR0_PAGING, 0x80000000
.set MSR_EFER, 0xc0000080
.set EFER_LONG_MODE, 0x00000100
.set EFER_NO_EXECUTE, 0x00000800

.set IDENTITY_GIGABYTES, 4
.set KERNEL_MAPPED_LARGE_PAGES, 16
.set DIRECT_MAP_TOP_LEVEL_INDEX, 256
.set KERNEL_TOP_LEVEL_INDEX, 511
.set KERNEL_DIRECTORY_POINTER_INDEX, 510

.set SELECTOR_CODE64, 0x08
.set SELECTOR_DATA64, 0x10

.section .boot.header, "a"
.align 8
barry_multiboot2_header:
    .long MULTIBOOT2_HEADER_MAGIC
    .long MULTIBOOT2_ARCHITECTURE_I386
    .long barry_multiboot2_header_end - barry_multiboot2_header
    .long -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCHITECTURE_I386 \
            + (barry_multiboot2_header_end - barry_multiboot2_header))

    .align 8
    .short 0
    .short 0
    .long 8
barry_multiboot2_header_end:

.section .boot.text, "ax"
.code32
.global barry_multiboot2_entry
.type barry_multiboot2_entry, @function
barry_multiboot2_entry:
    cli
    cld
    movl $boot_stack_top, %esp

    movl %eax, boot_magic
    movl %ebx, boot_info_address

    call build_identity_page_directories
    call build_identity_page_directory_pointers
    call build_kernel_page_directory
    call build_top_level_table
    call enter_long_mode

    ljmp $SELECTOR_CODE64, $long_mode_landing

build_identity_page_directories:
    movl $identity_page_directories, %edi
    movl $LARGE_LEAF_FLAGS, %eax
    movl $(IDENTITY_GIGABYTES * 512), %ecx
.fill_identity_leaf:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop .fill_identity_leaf
    ret

build_identity_page_directory_pointers:
    movl $identity_page_directory_pointers, %edi
    movl $identity_page_directories, %eax
    orl $TABLE_FLAGS, %eax
    movl $IDENTITY_GIGABYTES, %ecx
.fill_identity_pointer:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x1000, %eax
    addl $8, %edi
    loop .fill_identity_pointer
    ret

build_kernel_page_directory:
    movl $kernel_page_directory, %edi
    movl $(kernel_physical_base + LARGE_LEAF_FLAGS), %eax
    movl $KERNEL_MAPPED_LARGE_PAGES, %ecx
.fill_kernel_leaf:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop .fill_kernel_leaf

    movl $kernel_page_directory_pointers, %edi
    movl $kernel_page_directory, %eax
    orl $TABLE_FLAGS, %eax
    movl %eax, KERNEL_DIRECTORY_POINTER_INDEX * 8(%edi)
    movl $0, KERNEL_DIRECTORY_POINTER_INDEX * 8 + 4(%edi)
    ret

build_top_level_table:
    movl $top_level_table, %edi
    movl $identity_page_directory_pointers, %eax
    orl $TABLE_FLAGS, %eax
    movl %eax, (%edi)
    movl $0, 4(%edi)
    movl %eax, DIRECT_MAP_TOP_LEVEL_INDEX * 8(%edi)
    movl $0, DIRECT_MAP_TOP_LEVEL_INDEX * 8 + 4(%edi)

    movl $kernel_page_directory_pointers, %eax
    orl $TABLE_FLAGS, %eax
    movl %eax, KERNEL_TOP_LEVEL_INDEX * 8(%edi)
    movl $0, KERNEL_TOP_LEVEL_INDEX * 8 + 4(%edi)
    ret

enter_long_mode:
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4

    movl $top_level_table, %eax
    movl %eax, %cr3

    movl $MSR_EFER, %ecx
    rdmsr
    orl $(EFER_LONG_MODE | EFER_NO_EXECUTE), %eax
    wrmsr

    lgdt boot_gdt_pointer

    movl %cr0, %eax
    orl $CR0_PAGING, %eax
    movl %eax, %cr0
    ret

.code64
long_mode_landing:
    movw $SELECTOR_DATA64, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movl boot_magic, %r14d
    movl boot_info_address, %r15d

    movabsq $__kernel_bss_start, %rdi
    movabsq $__kernel_bss_end, %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    movabsq $barry_boot_stack_top, %rsp
    xorl %ebp, %ebp

    movl %r14d, %edi
    movl %r15d, %esi
    movabsq $barry_multiboot2_main, %rax
    callq *%rax

.halt_forever:
    cli
    hlt
    jmp .halt_forever

.size barry_multiboot2_entry, . - barry_multiboot2_entry

.section .boot.data, "aw"
.align 8
boot_magic:
    .long 0
boot_info_address:
    .long 0

.align 8
boot_gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
boot_gdt_end:

boot_gdt_pointer:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

.align 16
boot_stack:
    .space 4096
boot_stack_top:

.align 4096
top_level_table:
    .space 4096
identity_page_directory_pointers:
    .space 4096
kernel_page_directory_pointers:
    .space 4096
identity_page_directories:
    .space 4096 * IDENTITY_GIGABYTES
kernel_page_directory:
    .space 4096

.section .bss, "aw", @nobits
.align 16
.global barry_boot_stack
barry_boot_stack:
    .space 65536
.global barry_boot_stack_top
barry_boot_stack_top:
