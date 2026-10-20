.set SELECTOR_USER_CODE, 0x23
.set SELECTOR_USER_DATA, 0x1b

.set CONTEXT_RIP, 0
.set CONTEXT_RSP, 8
.set CONTEXT_RFLAGS, 16
.set CONTEXT_RDI, 24
.set CONTEXT_RSI, 32
.set CONTEXT_RDX, 40
.set CONTEXT_RCX, 48
.set CONTEXT_R8, 56
.set CONTEXT_R9, 64
.set CONTEXT_RAX, 72
.set CONTEXT_RBX, 80
.set CONTEXT_RBP, 88
.set CONTEXT_R10, 96
.set CONTEXT_R11, 104
.set CONTEXT_R12, 112
.set CONTEXT_R13, 120
.set CONTEXT_R14, 128
.set CONTEXT_R15, 136

.section .text
.global barry_enter_user_mode
.type barry_enter_user_mode, @function

barry_enter_user_mode:
    cli
    movq %rdi, %r11

    pushq $SELECTOR_USER_DATA
    pushq CONTEXT_RSP(%r11)
    pushq CONTEXT_RFLAGS(%r11)
    pushq $SELECTOR_USER_CODE
    pushq CONTEXT_RIP(%r11)

    movq CONTEXT_RDI(%r11), %rdi
    movq CONTEXT_RSI(%r11), %rsi
    movq CONTEXT_RDX(%r11), %rdx
    movq CONTEXT_RCX(%r11), %rcx
    movq CONTEXT_R8(%r11), %r8
    movq CONTEXT_R9(%r11), %r9
    movq CONTEXT_RAX(%r11), %rax
    movq CONTEXT_RBX(%r11), %rbx
    movq CONTEXT_RBP(%r11), %rbp
    movq CONTEXT_R10(%r11), %r10
    movq CONTEXT_R12(%r11), %r12
    movq CONTEXT_R13(%r11), %r13
    movq CONTEXT_R14(%r11), %r14
    movq CONTEXT_R15(%r11), %r15
    movq CONTEXT_R11(%r11), %r11

    swapgs
    iretq

.size barry_enter_user_mode, . - barry_enter_user_mode
