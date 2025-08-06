SHELL := /bin/bash

# Configuration
TARGET ?= hw
PLATFORM ?= xilinx_u280_gen3x16_xdma_1_202211_1
KERNEL := krnl_hash_dna

# Directories
BUILD_DIR := build_dir.$(TARGET).$(PLATFORM)
SRC_DIR := src

# Files
HOST_SRC := $(SRC_DIR)/host.cpp
KERNEL_SRC := $(SRC_DIR)/$(KERNEL).cpp
CONFIG_FILE := $(KERNEL).cfg

# Output files
XO_FILE := $(BUILD_DIR)/$(KERNEL).xo
XCLBIN_FILE := $(BUILD_DIR)/$(KERNEL).xclbin
HOST_EXE := host

# Tools
VPP := v++
CXX := g++

# Compiler flags
CXXFLAGS := -std=c++17 -O2 -Wall -g
LDFLAGS := -lxrt_coreutil -luuid -lpthread

# Vitis flags
VPP_FLAGS := -t $(TARGET) --platform $(PLATFORM) --save-temps -g

# Default target
all: build

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile kernel to XO
$(XO_FILE): $(KERNEL_SRC) $(CONFIG_FILE) | $(BUILD_DIR)
	$(VPP) -c $(VPP_FLAGS) \
		--config $(CONFIG_FILE) \
		-k $(KERNEL) \
		--temp_dir $(BUILD_DIR) \
		--output $(XO_FILE) $<

# Link XO to XCLBIN
$(XCLBIN_FILE): $(XO_FILE)
	$(VPP) -l $(VPP_FLAGS) \
		--config $(CONFIG_FILE) \
		--temp_dir $(BUILD_DIR) \
		--output $(XCLBIN_FILE) $(XO_FILE)

# Compile host
$(HOST_EXE): $(HOST_SRC)
	$(CXX) $(CXXFLAGS) -o $(HOST_EXE) $(HOST_SRC) $(LDFLAGS)

# Build everything
build: $(XCLBIN_FILE) $(HOST_EXE)

# Run on board
run: build
	./$(HOST_EXE) $(XCLBIN_FILE) 100

# Clean generated files
clean:
	rm -rf $(BUILD_DIR) $(HOST_EXE) *.log *.info *.wdb

# Full cleanup
cleanall: clean
	rm -rf _x* .Xil package.* *.csv *.json *.protoinst

# Help
help:
	@echo "Available targets:"
	@echo "  all     - Build everything (default)"
	@echo "  build   - Build kernel and host"
	@echo "  run     - Build and run on board"
	@echo "  clean   - Clean generated files"
	@echo "  cleanall- Full cleanup"
	@echo ""
	@echo "Variables:"
	@echo "  TARGET  - Build target (hw, hw_emu) [default: hw]"
	@echo "  PLATFORM- Platform name [default: xilinx_u280_gen3x16_xdma_1_202211_1]"

.PHONY: all build run clean cleanall help 