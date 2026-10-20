BOOT ?= limine
BUILD ?= build/$(BOOT)
ARCH := x86_64
CMDLINE ?=

CXX := clang++
LD := ld.lld
NM := llvm-nm
PYTHON := python3

LIMINE_DIR := third_party/limine
GRUB_MODULE_DIR ?= /usr/lib/grub/x86_64-efi
GRUB_MKIMAGE ?= grub2-mkimage
OVMF_DIR ?= /usr/share/edk2/ovmf
OVMF_CODE ?= $(OVMF_DIR)/OVMF_CODE.fd
OVMF_VARS_TEMPLATE ?= $(OVMF_DIR)/OVMF_VARS.fd

KERNEL := $(BUILD)/barry.elf
STAGE1_KERNEL := $(BUILD)/barry.stage1.elf
IMAGE := $(BUILD)/barry.hdd
OVMF_VARS := $(BUILD)/ovmf_vars.fd
LINKER_SCRIPT := kernel/arch/$(ARCH)/linker-$(BOOT).ld

WARNINGS := \
	-Wall -Wextra -Werror \
	-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align \
	-Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion \
	-Wdouble-promotion -Wimplicit-fallthrough -Wundef \
	-Wglobal-constructors -Wexit-time-destructors

FREESTANDING := \
	-target $(ARCH)-unknown-none \
	-ffreestanding -nostdlib -nostdinc++ \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-stack-protector -fno-stack-check \
	-fno-pic -fno-pie -fno-strict-aliasing \
	-fno-omit-frame-pointer \
	-mno-red-zone -mcmodel=kernel -mgeneral-regs-only

CXXFLAGS := -std=c++23 -O2 -g $(FREESTANDING) $(WARNINGS) -I. -I$(LIMINE_DIR)
ASFLAGS := -target $(ARCH)-unknown-none -ffreestanding -g -I.
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 --no-dynamic-linker

EXCLUDED_BACKENDS := -not -path 'kernel/boot/limine/*' -not -path 'kernel/boot/multiboot2/*'
CORE_SOURCES := $(shell find kernel -name '*.cpp' $(EXCLUDED_BACKENDS))
CORE_ASSEMBLY := $(shell find kernel -name '*.S' $(EXCLUDED_BACKENDS))
BOOT_SOURCES := $(wildcard kernel/boot/$(BOOT)/*.cpp)
BOOT_ASSEMBLY := $(wildcard kernel/boot/$(BOOT)/*.S)
SOURCES := $(CORE_SOURCES) $(BOOT_SOURCES)
ASSEMBLY := $(CORE_ASSEMBLY) $(BOOT_ASSEMBLY)
OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(SOURCES)) $(patsubst %.S,$(BUILD)/%.o,$(ASSEMBLY))

DUPLICATE_OBJECTS := $(strip $(shell printf '%s\n' $(notdir $(OBJECTS)) | sort | uniq -d))
ifneq ($(DUPLICATE_OBJECTS),)
$(error two sources map to the same object file: $(DUPLICATE_OBJECTS))
endif

STAGE1_SYMBOLS := $(BUILD)/symbols_stage1.cpp
FINAL_SYMBOLS := $(BUILD)/symbols_table.cpp

USER_CC := clang
USER_BUILD := build/userland
USER_PROGRAM := $(USER_BUILD)/hello
USER_CFLAGS := -target x86_64-unknown-linux-gnu -static -nostdlib -fno-pic \
	-ffreestanding -fno-stack-protector -mno-sse -O2 \
	-Wall -Wextra -Werror -fuse-ld=lld -Wl,-no-pie -Wl,-z,max-page-size=4096

INITRAMFS := $(USER_BUILD)/initramfs.cpio

EMBEDDED_IMAGE_SOURCE := $(BUILD)/embedded_user_image.S
EMBEDDED_IMAGE_OBJECT := $(BUILD)/embedded_user_image.o

ifeq ($(BOOT),limine)
BOOT_CONFIG := $(BUILD)/limine.conf
BOOT_CONFIG_TEMPLATE := boot/limine.conf.in
EFI_LOADER := $(LIMINE_DIR)/BOOTX64.EFI
IMAGE_CONTENTS := \
	--add $(EFI_LOADER):EFI/BOOT/BOOTX64.EFI \
	--add $(KERNEL):boot/barry.elf \
	--add $(BOOT_CONFIG):boot/limine/limine.conf
endif

ifeq ($(BOOT),multiboot2)
BOOT_CONFIG := $(BUILD)/grub.cfg
BOOT_CONFIG_TEMPLATE := boot/grub.cfg.in
EFI_LOADER := $(BUILD)/grubx64.efi
GRUB_MODULES := multiboot2 part_msdos part_gpt fat normal configfile echo test ls all_video
IMAGE_CONTENTS := \
	--add $(EFI_LOADER):EFI/BOOT/BOOTX64.EFI \
	--add $(KERNEL):boot/barry.elf \
	--add $(BOOT_CONFIG):boot/grub/grub.cfg
endif

QEMU := qemu-system-$(ARCH)
QEMU_MACHINE := -machine q35 -cpu qemu64 -smp 4 -m 512M
QEMU_FIRMWARE := \
	-drive if=pflash,unit=0,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,unit=1,format=raw,file=$(OVMF_VARS)
QEMU_DISK := \
	-drive file=$(IMAGE),format=raw,if=none,id=disk0 \
	-device virtio-blk-pci,drive=disk0
QEMU_COMMON := $(QEMU_MACHINE) $(QEMU_FIRMWARE) $(QEMU_DISK) -no-reboot
QEMU_DEBUG_EXIT := -device isa-debug-exit,iobase=0xf4,iosize=0x04

BOOT_TEST_LOG := $(BUILD)/boot-test.log
BOOT_TEST_TIMEOUT ?= 120
BOOT_TEST_SUCCESS_CODE := 1

.PHONY: all kernel image run boot-test boot-test-all host-test test clean tidy format help

all: image

kernel: $(KERNEL)

image: $(IMAGE)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	@$(CXX) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.symobj.o: $(BUILD)/%.cpp
	@echo "  CXX   $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(STAGE1_SYMBOLS):
	@mkdir -p $(dir $@)
	@echo "  GEN   $@"
	@$(PYTHON) tools/generate_symbols.py --output $@

$(USER_PROGRAM): userland/hello.c
	@mkdir -p $(dir $@)
	@echo "  USER  $@"
	@$(USER_CC) $(USER_CFLAGS) -o $@ $<

$(INITRAMFS): $(USER_PROGRAM) tools/make_initramfs.py
	@mkdir -p $(dir $@)
	@echo "  CPIO  $@"
	@$(PYTHON) tools/make_initramfs.py --output $@ --add $(USER_PROGRAM):bin/hello

$(EMBEDDED_IMAGE_SOURCE): $(USER_PROGRAM) $(INITRAMFS)
	@mkdir -p $(dir $@)
	@echo "  GEN   $@"
	@printf '%s\n' \
		'.section .rodata' \
		'.align 16' \
		'.global barry_embedded_user_image_start' \
		'barry_embedded_user_image_start:' \
		'    .incbin "$(USER_PROGRAM)"' \
		'.global barry_embedded_user_image_end' \
		'barry_embedded_user_image_end:' \
		'.align 16' \
		'.global barry_embedded_initramfs_start' \
		'barry_embedded_initramfs_start:' \
		'    .incbin "$(INITRAMFS)"' \
		'.global barry_embedded_initramfs_end' \
		'barry_embedded_initramfs_end:' > $@

$(EMBEDDED_IMAGE_OBJECT): $(EMBEDDED_IMAGE_SOURCE)
	@echo "  AS    $<"
	@$(CXX) $(ASFLAGS) -c $< -o $@

$(STAGE1_KERNEL): $(OBJECTS) $(EMBEDDED_IMAGE_OBJECT) $(STAGE1_SYMBOLS:.cpp=.symobj.o) $(LINKER_SCRIPT)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS) $(EMBEDDED_IMAGE_OBJECT) \
		$(STAGE1_SYMBOLS:.cpp=.symobj.o)

$(FINAL_SYMBOLS): $(STAGE1_KERNEL)
	@echo "  GEN   $@"
	@$(PYTHON) tools/generate_symbols.py --nm $(NM) --elf $< --output $@

$(KERNEL): $(OBJECTS) $(EMBEDDED_IMAGE_OBJECT) $(FINAL_SYMBOLS:.cpp=.symobj.o) $(LINKER_SCRIPT)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS) $(EMBEDDED_IMAGE_OBJECT) \
		$(FINAL_SYMBOLS:.cpp=.symobj.o)
	@echo "  CHECK symbol table is stable across link passes"
	@$(PYTHON) tools/generate_symbols.py --nm $(NM) --elf $(STAGE1_KERNEL) \
		--verify-against $@ --output $(BUILD)/symbols_verify.cpp

$(BOOT_CONFIG): $(BOOT_CONFIG_TEMPLATE)
	@mkdir -p $(dir $@)
	@echo "  GEN   $@"
	@sed 's|@CMDLINE@|$(CMDLINE)|' $< > $@

$(BUILD)/grubx64.efi:
	@mkdir -p $(dir $@)
	@echo "  GRUB  $@"
	@$(GRUB_MKIMAGE) -O x86_64-efi -d $(GRUB_MODULE_DIR) -p /boot/grub -o $@ $(GRUB_MODULES)

$(IMAGE): $(KERNEL) $(BOOT_CONFIG) $(EFI_LOADER)
	@echo "  IMAGE $@"
	@$(PYTHON) tools/make_disk_image.py --output $@ --size-mib 64 $(IMAGE_CONTENTS)

$(OVMF_VARS):
	@mkdir -p $(dir $@)
	@cp $(OVMF_VARS_TEMPLATE) $@

run: $(IMAGE) $(OVMF_VARS)
	$(QEMU) $(QEMU_COMMON) -serial stdio -display none -no-shutdown

boot-test:
	@$(MAKE) --no-print-directory CMDLINE="barry.autotest $(CMDLINE)" $(IMAGE) $(OVMF_VARS)
	@echo "  BOOT  $(BOOT) backend under QEMU"
	@timeout $(BOOT_TEST_TIMEOUT) $(QEMU) $(QEMU_COMMON) $(QEMU_DEBUG_EXIT) \
		-serial file:$(BOOT_TEST_LOG) -display none >/dev/null 2>&1; \
	status=$$?; \
	sed -n '/^Running /,$$p' $(BOOT_TEST_LOG) || true; \
	if [ "$$status" != "$(BOOT_TEST_SUCCESS_CODE)" ]; then \
		echo "FAILED: qemu exit status $$status (expected $(BOOT_TEST_SUCCESS_CODE))"; \
		exit 1; \
	fi; \
	echo "PASSED: $(BOOT) backend"

boot-test-all:
	@$(MAKE) --no-print-directory BOOT=limine boot-test
	@$(MAKE) --no-print-directory BOOT=multiboot2 boot-test

host-test:
	@$(MAKE) --no-print-directory -C tests/host

test: host-test boot-test-all

tidy:
	clang-tidy $(SOURCES) -- $(CXXFLAGS)

format:
	clang-format -i $(shell find kernel tests -name '*.cpp' -o -name '*.hpp')

clean:
	rm -rf build

help:
	@echo "Barry build targets:"
	@echo "  make kernel [BOOT=limine|multiboot2]   build the kernel ELF"
	@echo "  make image                             build a bootable disk image"
	@echo "  make run                               boot the image in QEMU on the terminal"
	@echo "  make boot-test                         boot in QEMU and assert the tests pass"
	@echo "  make boot-test-all                     boot-test every backend"
	@echo "  make host-test                         run the host unit tests with sanitizers"
	@echo "  make test                              host tests plus every backend boot test"
	@echo "  make tidy / make format                static analysis / formatting"

-include $(shell find build -name '*.d' 2>/dev/null)
