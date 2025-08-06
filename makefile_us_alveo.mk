# Platform target (U280)
PLATFORM ?= xilinx_u280_gen3x16_xdma_1_202211_1

# Kernel name
KERNEL := krnl_hash_dna

# Source files
HOST_SRC := src/host.cpp
KERNEL_SRC := src/krnl_hash.cpp
CONFIG_FILE := krnl_hash.cfg

# Build targets
BUILD_DIR := build_dir.$(TARGET).$(PLATFORM)
XO_FILE := $(BUILD_DIR)/$(KERNEL).xo
XCLBIN_FILE := $(BUILD_DIR)/$(KERNEL).xclbin

# Compiler options
CXXFLAGS := -Wall -O2 -g -std=c++17
LDFLAGS := -lxrt_coreutil -luuid -lpthread

# Xilinx tools
VPP := v++
CXX := g++

# Default target
all: build

# Step 1: Compile kernel to XO
$(XO_FILE): $(KERNEL_SRC)
	$(VPP) -c -t $(TARGET) --platform $(PLATFORM) \
		--save-temps -g \
		--config $(CONFIG_FILE) \
		-k $(KERNEL) \
		--temp_dir $(BUILD_DIR) \
		--output $(XO_FILE) $<

# Step 2: Link XO to XCLBIN
$(XCLBIN_FILE): $(XO_FILE)
	$(VPP) -l -t $(TARGET) --platform $(PLATFORM) \
		--save-temps -g \
		--config $(CONFIG_FILE) \
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
