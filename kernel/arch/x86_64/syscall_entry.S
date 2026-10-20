.section .text

.set PROCESSOR_LOCAL_KERNEL_STACK_OFFSET, 8
.set PROCESSOR_LOCAL_USER_STACK_OFFSET, 16

.global barry_syscall_entry
.type barry_syscall_entry, @function

barry_syscall_entry:
    swapgs
    movq %rsp, %gs:PROCESSOR_LOCAL_USER_STACK_OFFSET
    movq %gs:PROCESSOR_LOCAL_KERNEL_STACK_OFFSET, %rsp

    pushq %gs:PROCESSOR_LOCAL_USER_STACK_OFFSET
    pushq %r11
    pushq %rcx

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

    cld
    movq %rsp, %rdi
    call barry_handle_syscall

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

    popq %rcx
    popq %r11
    popq %rsp

    swapgs
    sysretq

.size barry_syscall_entry, . - barry_syscall_entry
