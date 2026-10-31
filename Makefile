# Thin wrappers around the three CMake projects. See docs/designs/phase-1.md.

PICO_TOOLCHAIN_PATH ?= $(wildcard $(HOME)/.local/arm-gnu-toolchain/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi)
PICO_SDK_TAG ?= 2.3.1
ARM_SIZE := $(if $(PICO_TOOLCHAIN_PATH),$(PICO_TOOLCHAIN_PATH)/bin/,)arm-none-eabi-size
APP := build/mac/Robot Meeting Alert.app

.PHONY: all test mac run firmware size flash clean

all: test mac firmware

test:
	cmake -S tests -B build/tests -G Ninja
	cmake --build build/tests
	ctest --test-dir build/tests --output-on-failure

mac:
	cmake -S mac -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build/mac

run: mac
	open "$(APP)"

firmware:
	cmake -S firmware -B build/firmware -G Ninja \
		-DPICO_SDK_FETCH_FROM_GIT=ON -DPICO_SDK_FETCH_FROM_GIT_TAG:STRING=$(PICO_SDK_TAG) \
		$(if $(PICO_TOOLCHAIN_PATH),-DPICO_TOOLCHAIN_PATH=$(PICO_TOOLCHAIN_PATH))
	cmake --build build/firmware

size: firmware
	$(ARM_SIZE) build/firmware/rma_firmware.elf

flash: firmware
	picotool load -x -f build/firmware/rma_firmware.uf2

clean:
	rm -rf build
