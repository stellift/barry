OUTPUT_FORMAT(elf64-x86-64)
ENTRY(barry_multiboot2_entry)

KERNEL_VIRTUAL_BASE = 0xffffffff80000000;
KERNEL_PHYSICAL_BASE = 0x200000;
BOOT_PHYSICAL_BASE = 0x100000;

kernel_physical_base = KERNEL_PHYSICAL_BASE;

PHDRS
{
    boot    PT_LOAD FLAGS((1 << 0) | (1 << 1) | (1 << 2));
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD FLAGS((1 << 2));
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = BOOT_PHYSICAL_BASE;
    __boot_image_start = .;

    .boot : {
        KEEP(*(.boot.header))
        *(.boot.text)
        *(.boot.data)
    } :boot

    . = ALIGN(4096);
    __boot_image_end = .;

    . = KERNEL_VIRTUAL_BASE;
    __kernel_virtual_base = .;

    .text : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        __kernel_text_start = .;
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_text_end = .;

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        __kernel_readonly_start = .;
        *(.rodata .rodata.*)
    } :rodata

    .init_array : AT(ADDR(.init_array) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        KEEP(*(.init_array .init_array.*))
        KEEP(*(.ctors))
        KEEP(*(.preinit_array))
    } :rodata

    .ktests : AT(ADDR(.ktests) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        __kernel_tests_start = .;
        KEEP(*(.ktests))
        __kernel_tests_end = .;
    } :rodata

    .ksyms : AT(ADDR(.ksyms) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        KEEP(*(.ksyms))
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_readonly_end = .;

    .data : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        __kernel_writable_start = .;
        *(.data .data.*)
    } :data

    .bss : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE) {
        __kernel_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __kernel_bss_end = .;
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
