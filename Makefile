BOOT ?= limine
BUILD ?= build
ARCH := x86_64

CXX := clang++
LD := ld.lld
NM := llvm-nm
PYTHON := python3

LIMINE_DIR := third_party/limine
OVMF_DIR := /usr/share/edk2/ovmf

IMAGE := $(BUILD)/barry.hdd
KERNEL := $(BUILD)/barry.elf
STAGE1_KERNEL := $(BUILD)/barry.stage1.elf
LINKER_SCRIPT := kernel/arch/$(ARCH)/linker-$(BOOT).ld

WARNINGS := \
	-Wall -Wextra -Werror \
	-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align \
	-Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion \
	-Wdouble-promotion -Wimplicit-fallthrough -Wundef

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

CORE_SOURCES := $(shell find kernel -name '*.cpp' -not -path 'kernel/boot/limine/*' \
	-not -path 'kernel/boot/multiboot/*')
CORE_ASSEMBLY := $(shell find kernel -name '*.S' -not -path 'kernel/boot/limine/*' \
	-not -path 'kernel/boot/multiboot/*')
BOOT_SOURCES := $(wildcard kernel/boot/$(BOOT)/*.cpp)
BOOT_ASSEMBLY := $(wildcard kernel/boot/$(BOOT)/*.S)
SOURCES := $(CORE_SOURCES) $(BOOT_SOURCES)
ASSEMBLY := $(CORE_ASSEMBLY) $(BOOT_ASSEMBLY)
OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(SOURCES)) $(patsubst %.S,$(BUILD)/%.o,$(ASSEMBLY))

STAGE1_SYMBOLS := $(BUILD)/symbols_stage1.cpp
FINAL_SYMBOLS := $(BUILD)/symbols_table.cpp

QEMU := qemu-system-x86_64
QEMU_FLAGS := \
	-machine q35 -cpu qemu64 -smp 4 -m 512M \
	-drive if=pflash,unit=0,format=raw,readonly=on,file=$(OVMF_DIR)/OVMF_CODE.fd \
	-drive if=pflash,unit=1,format=raw,file=$(BUILD)/ovmf_vars.fd \
	-drive file=$(IMAGE),format=raw,if=none,id=disk0 \
	-device virtio-blk-pci,drive=disk0 \
	-serial stdio -display none -no-reboot -no-shutdown

.PHONY: all kernel image run test clean tidy format

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

$(STAGE1_KERNEL): $(OBJECTS) $(STAGE1_SYMBOLS:.cpp=.symobj.o) $(LINKER_SCRIPT)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS) $(STAGE1_SYMBOLS:.cpp=.symobj.o)

$(FINAL_SYMBOLS): $(STAGE1_KERNEL)
	@echo "  GEN   $@"
	@$(PYTHON) tools/generate_symbols.py --nm $(NM) --elf $< --output $@

$(KERNEL): $(OBJECTS) $(FINAL_SYMBOLS:.cpp=.symobj.o) $(LINKER_SCRIPT)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS) $(FINAL_SYMBOLS:.cpp=.symobj.o)
	@echo "  VERIFY symbol table"
	@$(PYTHON) tools/generate_symbols.py --nm $(NM) --elf $(STAGE1_KERNEL) \
		--verify-against $@ --output $(BUILD)/symbols_verify.cpp

$(IMAGE): $(KERNEL) boot/limine.conf
	@echo "  IMAGE $@"
	@$(PYTHON) tools/make_disk_image.py --output $@ --size-mib 64 \
		--add $(LIMINE_DIR)/BOOTX64.EFI:EFI/BOOT/BOOTX64.EFI \
		--add $(KERNEL):boot/barry.elf \
		--add boot/limine.conf:boot/limine/limine.conf

$(BUILD)/ovmf_vars.fd:
	@mkdir -p $(dir $@)
	cp $(OVMF_DIR)/OVMF_VARS.fd $@

run: $(IMAGE) $(BUILD)/ovmf_vars.fd
	$(QEMU) $(QEMU_FLAGS)

test:
	$(MAKE) -C tests/host

tidy:
	clang-tidy $(SOURCES) -- $(CXXFLAGS)

format:
	clang-format -i $(shell find kernel tests -name '*.cpp' -o -name '*.hpp')

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
