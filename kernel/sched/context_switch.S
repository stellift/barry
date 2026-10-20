.section .text

.global barry_switch_context
.type barry_switch_context, @function

barry_switch_context:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushfq

    movq %rsp, (%rdi)
    movq %rsi, %rsp

    popfq
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

.size barry_switch_context, . - barry_switch_context

.global barry_thread_trampoline
.type barry_thread_trampoline, @function

barry_thread_trampoline:
    xorl %ebp, %ebp
    call barry_enter_thread

.thread_trampoline_unreachable:
    cli
    hlt
    jmp .thread_trampoline_unreachable

.size barry_thread_trampoline, . - barry_thread_trampoline
