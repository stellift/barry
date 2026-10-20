OUTPUT_FORMAT(elf64-x86-64)
ENTRY(barry_limine_entry)

PHDRS
{
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD FLAGS((1 << 2));
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_virtual_base = .;

    .text : {
        __kernel_text_start = .;
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_text_end = .;

    .rodata : {
        __kernel_readonly_start = .;
        *(.rodata .rodata.*)
    } :rodata

    .init_array : {
        KEEP(*(.init_array .init_array.*))
        KEEP(*(.ctors))
        KEEP(*(.preinit_array))
    } :rodata

    .ktests : {
        __kernel_tests_start = .;
        KEEP(*(.ktests))
        __kernel_tests_end = .;
    } :rodata

    .ksyms : {
        KEEP(*(.ksyms))
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_readonly_end = .;

    .limine_requests : {
        __kernel_writable_start = .;
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_writable_end = .;
    __kernel_image_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
        *(.interp)
        *(.dynamic)
    }
}

ASSERT(SIZEOF(.init_array) == 0,
       "Barry forbids globals that need dynamic initialization; make them constant-initializable")
