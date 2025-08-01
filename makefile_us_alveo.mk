# Platform target (U280)
XSA := xilinx_u280_gen3x16_xdma_1_202211_1

# Kernel name
KERNEL := krnl_hash_dna

# Source files
HOST_SRC := host.cpp
KERNEL_SRC := $(KERNEL).cpp

# Build targets
BUILD_DIR := build_dir.$(TARGET).$(XSA)
XO_FILE := $(BUILD_DIR)/$(KERNEL).xo
XCLBIN_FILE := $(BUILD_DIR)/$(KERNEL).xclbin

# Compiler options
CXXFLAGS := -Wall -O2 -g -std=c++17
LDFLAGS := -lxilinxopencl -pthread -lrt

# Xilinx tools
VPP := v++
CXX := g++

# Default target
all: build

# Step 1: Compile kernel to XO
$(XO_FILE): $(KERNEL_SRC)
	$(VPP) -c -t $(TARGET) --platform $(XSA) \
		--save-temps -g \
		--config $(KERNEL).cfg \
		-k $(KERNEL) \
		--temp_dir $(BUILD_DIR) \
		--output $(XO_FILE) $<

# Step 2: Link XO to XCLBIN
$(XCLBIN_FILE): $(XO_FILE)
	$(VPP) -l -t $(TARGET) --platform $(XSA) \
		--save-temps -g \
		--config $(KERNEL).cfg \
		--temp_dir $(BUILD_DIR) \
		--output $(XCLBIN_FILE) $(XO_FILE)

# Step 3: Compile host
host: $(HOST_SRC)
	$(CXX) $(CXXFLAGS) -o host $(HOST_SRC) $(LDFLAGS)

# Build everything
build: $(XCLBIN_FILE) host

# Run on board
run: build
	XCL_EMULATION_MODE=$(TARGET) ./host $(XCLBIN_FILE)

# Clean generated files
clean:
	rm -rf build_dir.* *.xclbin *.xo *.log *.info *.wdb host

# Full cleanup
cleanall: clean
	rm -rf _x* .Xil package.* *.csv *.json *.protoinst
