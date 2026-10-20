ROOT := ../..
BUILD := $(ROOT)/build/host
BINARY := $(BUILD)/barry-host-tests

CXX := clang++

WARNINGS := \
	-Wall -Wextra -Werror \
	-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align \
	-Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion \
	-Wimplicit-fallthrough -Wundef

SANITIZERS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

CXXFLAGS := -std=c++23 -g -O1 $(WARNINGS) $(SANITIZERS) \
	-DBARRY_HOSTED -DBARRY_HOST_TEST -DBARRY_DEBUG -I$(ROOT)

PORTABLE_SOURCES := \
	$(ROOT)/kernel/base/bytes.cpp \
	$(ROOT)/kernel/base/bitmap.cpp \
	$(ROOT)/kernel/base/format.cpp \
	$(ROOT)/kernel/base/error.cpp \
	$(ROOT)/kernel/boot/command_line.cpp \
	$(ROOT)/kernel/boot/boot_info.cpp \
	$(ROOT)/kernel/boot/boot_info_builder.cpp \
	$(ROOT)/kernel/boot/memory_reservations.cpp \
	$(ROOT)/kernel/test/base_tests.cpp \
	$(ROOT)/kernel/test/boot_tests.cpp \
	harness.cpp

.PHONY: all run clean

all: run

$(BINARY): $(PORTABLE_SOURCES)
	@mkdir -p $(BUILD)
	@echo "  CXX   host test binary"
	@$(CXX) $(CXXFLAGS) $(PORTABLE_SOURCES) -o $@

run: $(BINARY)
	@ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 $(BINARY)

clean:
	rm -rf $(BUILD)
