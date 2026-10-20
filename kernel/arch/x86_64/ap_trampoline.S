.set SELECTOR_CODE32, 0x08
.set SELECTOR_DATA32, 0x10
.set SELECTOR_CODE64, 0x18

.set CR0_PROTECTED_MODE, 0x00000001
.set CR0_PAGING, 0x80000000
.set CR4_PAE, 0x00000020
.set MSR_EFER, 0xc0000080
.set EFER_LONG_MODE, 0x00000100
.set EFER_NO_EXECUTE, 0x00000800

.set OFFSET_GDT_TABLE, gdt_table - trampoline_entry
.set OFFSET_GDT_POINTER, gdt_pointer - trampoline_entry
.set OFFSET_GDT_POINTER_BASE, gdt_pointer_base - trampoline_entry
.set OFFSET_PROTECTED_JUMP, protected_jump_pointer - trampoline_entry
.set OFFSET_PROTECTED_JUMP_TARGET, protected_jump_target - trampoline_entry
.set OFFSET_LONG_JUMP, long_jump_pointer - trampoline_entry
.set OFFSET_LONG_JUMP_TARGET, long_jump_target - trampoline_entry
.set OFFSET_PAGE_TABLE_ROOT, parameter_page_table_root - trampoline_entry
.set OFFSET_STACK_TOP, parameter_stack_top - trampoline_entry
.set OFFSET_ENTRY_POINT, parameter_entry_point - trampoline_entry
.set OFFSET_ARGUMENT, parameter_argument - trampoline_entry
.set OFFSET_READY_FLAG, parameter_ready_flag - trampoline_entry
.set OFFSET_PROTECTED_ENTRY, protected_entry - trampoline_entry
.set OFFSET_LONG_MODE_ENTRY, long_mode_entry - trampoline_entry

.section .text
.align 16
.code16

.global barry_ap_trampoline_start
barry_ap_trampoline_start:
trampoline_entry:
    jmp real_mode_entry

    .align 16
gdt_table:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
gdt_table_end:

gdt_pointer:
    .word gdt_table_end - gdt_table - 1
gdt_pointer_base:
    .long 0

protected_jump_pointer:
protected_jump_target:
    .long 0
    .word SELECTOR_CODE32

long_jump_pointer:
long_jump_target:
    .long 0
    .word SELECTOR_CODE64

    .align 8
parameter_page_table_root:
    .quad 0
parameter_stack_top:
    .quad 0
parameter_entry_point:
    .quad 0
parameter_argument:
    .quad 0
parameter_ready_flag:
    .quad 0

real_mode_entry:
    cli
    cld

    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %sp, %sp

    movzwl %ax, %ebx
    shll $4, %ebx

    leal OFFSET_GDT_TABLE(%ebx), %eax
    movl %eax, OFFSET_GDT_POINTER_BASE

    leal OFFSET_PROTECTED_ENTRY(%ebx), %eax
    movl %eax, OFFSET_PROTECTED_JUMP_TARGET

    leal OFFSET_LONG_MODE_ENTRY(%ebx), %eax
    movl %eax, OFFSET_LONG_JUMP_TARGET

    lgdtl OFFSET_GDT_POINTER

    movl %cr0, %eax
    orl $CR0_PROTECTED_MODE, %eax
    movl %eax, %cr0

    ljmpl *OFFSET_PROTECTED_JUMP

.code32
protected_entry:
    movw $SELECTOR_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movl OFFSET_PAGE_TABLE_ROOT(%ebx), %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4

    movl $MSR_EFER, %ecx
    rdmsr
    orl $(EFER_LONG_MODE | EFER_NO_EXECUTE), %eax
    wrmsr

    movl %cr0, %eax
    orl $CR0_PAGING, %eax
    movl %eax, %cr0

    ljmpl *OFFSET_LONG_JUMP(%ebx)

.code64
long_mode_entry:
    movq OFFSET_STACK_TOP(%rbx), %rsp
    xorl %ebp, %ebp

    movq OFFSET_ARGUMENT(%rbx), %rdi
    movq OFFSET_ENTRY_POINT(%rbx), %rax

    movq $1, OFFSET_READY_FLAG(%rbx)

    callq *%rax

application_processor_halt:
    cli
    hlt
    jmp application_processor_halt

.global barry_ap_trampoline_end
barry_ap_trampoline_end:

.section .rodata
.align 8
.global barry_ap_trampoline_offsets
barry_ap_trampoline_offsets:
    .quad OFFSET_PAGE_TABLE_ROOT
    .quad OFFSET_STACK_TOP
    .quad OFFSET_ENTRY_POINT
    .quad OFFSET_ARGUMENT
    .quad OFFSET_READY_FLAG
