.section .text
.global barry_reload_segments
.type barry_reload_segments, @function

barry_reload_segments:
    movw %si, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movzwq %di, %rax
    leaq .Lreloaded(%rip), %rcx
    pushq %rax
    pushq %rcx
    lretq

.Lreloaded:
    ret
.size barry_reload_segments, . - barry_reload_segments
