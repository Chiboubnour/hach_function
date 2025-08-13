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
KERNEL_NAME = krnl_hach
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
ifeq ($(TARGET),hw_emu)
	XCL_EMULATION_MODE=hw_emu ./$(HOST_EXE) $(XCLBIN) 0
else
	./$(HOST_EXE) $(XCLBIN) 0
endif

# Clean up
clean:
	rm -rf $(HOST_EXE) *.xo *.log *.json *.info .Xil _x

cleanall: clean
	rm -rf *.xclbin

help:
	@echo "Makefile Usage:"
	@echo "  make all TARGET=<hw_emu/hw> PLATFORM=<FPGA platform>   # build everything"
	@echo "  make build TARGET=<hw_emu/hw> PLATFORM=<FPGA platform> # build kernel and host"
	@echo "  make host                                              # build host only"
	@echo "  make run TARGET=<hw_emu/hw> PLATFORM=<FPGA platform>   # run the app"
	@echo "  make clean                                             # remove build artifacts"
	@echo "  make cleanall                                          # remove all generated files"
