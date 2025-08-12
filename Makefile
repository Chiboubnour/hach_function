SHELL := /bin/bash
MK_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PWD      = $(shell readlink -f .)

######################## Project Variables ########################
TARGET    ?= hw
PLATFORM  ?= xilinx_u280_gen3x16_xdma_1_202211_1
HOST_ARCH := x86

# Host source and executable
HOST_SRC  = src/host.cpp
HOST_EXE  = host.exe

# Kernel source and output
KERNEL_SRC  = src/krnl_hach.cpp
KERNEL_NAME = hach_sequence
XO_FILE     = $(KERNEL_NAME).xo
XCLBIN      = $(KERNEL_NAME).$(TARGET).xclbin

# Tools and flags
VPP       = v++
CXX       = g++
CXXFLAGS  = -Wall -O2 -std=c++14 -I$(XILINX_XRT)/include
LDFLAGS   = -L$(XILINX_XRT)/lib -lxrt_coreutil -pthread -lrt -lstdc++

########################### Make Targets ###########################

all: build

build: $(XCLBIN) $(HOST_EXE)

# Step 1: Compile kernel to XO
$(XO_FILE): $(KERNEL_SRC)
	$(VPP) -c -t $(TARGET) --platform $(PLATFORM) \
		-k $(KERNEL_NAME) \
		--kernel_frequency 300 \
		-o $@ $<

# Step 2: Link XO to XCLBIN
$(XCLBIN): $(XO_FILE)
	$(VPP) -l -t $(TARGET) --platform $(PLATFORM) \
		--kernel_frequency 300 \
		-o $@ $<

# Build host only
$(HOST_EXE): $(HOST_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

host: $(HOST_EXE)

# Run application
run: $(HOST_EXE) $(XCLBIN)
	XCL_EMULATION_MODE=$(TARGET) ./$(HOST_EXE) $(XCLBIN)

# Clean up
clean:
	rm -rf $(HOST_EXE) *.xo *.log *.json *.info .Xil _x

cleanall: clean
	rm -rf *.xclbin

help:
	@echo "Makefile Usage:"
	@echo "  make all TARGET=<hw_emu/hw> PLATFORM=<FPGA platform>"
	@echo "  make run TARGET=<hw_emu/hw> PLATFORM=<FPGA platform>"
	@echo "  make build   - build xclbin and host"
	@echo "  make host    - build host only"
	@echo "  make clean   - remove non-hardware files"
	@echo "  make cleanall- remove all generated files"
