#include "cmdlineparser.h"
#include <iostream>
#include <cstring>
#include <vector>

// XRT includes
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

#include <stdint.h>
#include <stdlib.h>
#include <chrono>
#include <algorithm>


double run_krnl(xrtDeviceHandle device, xrt::kernel& krnl, int bank_assign[2], unsigned int n) {
    size_t input_size_bytes = ((n + 7) / 8) * sizeof(uint64_t); // packed sequence size
    size_t n_smers = n - 27; // S=28, so n_smers = n-(S-1)
    size_t output_size_bytes = n_smers * sizeof(uint64_t);

    std::cout << "Allocating buffers in global memory\n";
    auto bo_seq = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);

    auto seq_map = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();

    // Initialize input buffer with test data - example: fill with random or real packed sequence
    std::cout << "Initializing input sequence buffer\n";
    for (size_t i = 0; i < input_size_bytes / sizeof(uint64_t); i++) {
        seq_map[i] = 0x4141414141414141ULL; // example: all 'A' (ASCII 0x41) packed repeatedly
    }
    // Zero output buffer
    std::memset(hash_map, 0, output_size_bytes);

    std::cout << "Sync input buffer to device\n";
    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    std::chrono::duration<double> kernel_time(0);

    std::cout << "Launching kernel\n";
    auto kernel_start = std::chrono::high_resolution_clock::now();

    auto run = krnl(bo_seq, n, bo_hash);
    run.wait();

    auto kernel_end = std::chrono::high_resolution_clock::now();
    kernel_time = kernel_end - kernel_start;

    std::cout << "Sync output buffer from device\n";
    bo_hash.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    // Optional: print first few hashes for verification
    std::cout << "First 10 hashes output:\n";
    for (size_t i = 0; i < std::min<size_t>(10, n_smers); i++) {
        std::cout << std::hex << hash_map[i] << std::dec << std::endl;
    }

    return kernel_time.count();
}

int main(int argc, char* argv[]) {
    // Command Line Parser
    sda::utils::CmdLineParser parser;

    parser.addSwitch("--xclbin_file", "-x", "input binary file string", "");
    parser.addSwitch("--device_id", "-d", "device index", "0");
    parser.parse(argc, argv);

    std::string binaryFile = parser.value("xclbin_file");
    int device_index = stoi(parser.value("device_id"));

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    std::cout << "Open device " << device_index << std::endl;
    auto device = xrt::device(device_index);

    std::cout << "Load xclbin " << binaryFile << std::endl;
    auto uuid = device.load_xclbin(binaryFile);

    auto krnl = xrt::kernel(device, uuid, "hach_sequence");

    // Test sequence length (example)
    const unsigned int n = 1024;  // You can change this to your real sequence length

    // Bank assignments: you can assign your buffers to HBM banks 0 and 1 for example
    int bank_assign[2] = {0, 1};

    double kernel_time_in_sec = run_krnl(device, krnl, bank_assign, n);

    size_t n_smers = n - 27; // S=28
    double total_bytes = ( ((n + 7)/8) + n_smers ) * sizeof(uint64_t);

    double throughput = total_bytes / kernel_time_in_sec / 1e9; // GB/s

    std::cout << "Kernel execution time: " << kernel_time_in_sec << " sec\n";
    std::cout << "Throughput: " << throughput << " GB/s\n";

    std::cout << "Test completed successfully." << std::endl;

    return 0;
}
