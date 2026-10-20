.section .text

.set VECTOR_COUNT, 256
.set STUB_STRIDE, 16
.set TRAP_FRAME_CODE_SELECTOR_OFFSET, 144
.set USER_PRIVILEGE_MASK, 3

.global barry_isr_stub_table
.global barry_isr_stub_stride

.align 16
barry_isr_stub_table:
.set current_vector, 0
.rept VECTOR_COUNT
    .align STUB_STRIDE
    .if (current_vector == 8) || (current_vector == 10) || (current_vector == 11) || \
        (current_vector == 12) || (current_vector == 13) || (current_vector == 14) || \
        (current_vector == 17) || (current_vector == 21) || (current_vector == 29) || \
        (current_vector == 30)
    .else
        pushq $0
    .endif
    pushq $current_vector
    jmp barry_isr_common
    .set current_vector, current_vector + 1
.endr

barry_isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    testb $USER_PRIVILEGE_MASK, TRAP_FRAME_CODE_SELECTOR_OFFSET(%rsp)
    jz .Lkernel_entry
    swapgs
.Lkernel_entry:

    cld
    movq %rsp, %rdi
    call barry_dispatch_interrupt

    testb $USER_PRIVILEGE_MASK, TRAP_FRAME_CODE_SELECTOR_OFFSET(%rsp)
    jz .Lkernel_exit
    swapgs
.Lkernel_exit:

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq

.section .rodata
.align 8
barry_isr_stub_stride:
    .quad STUB_STRIDE
