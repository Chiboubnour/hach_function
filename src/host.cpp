#include <iostream>
#include <vector>
#include <xrt/xrt_kernel.h>
#include <xrt/xrt_bo.h>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <xclbin> <sequence_length>" << std::endl;
        return 1;
    }

    std::string binaryFile = argv[1];
    int sequence_length = std::stoi(argv[2]);
    int k = 28;
    int n_sm = sequence_length - (k - 1);

    auto device = xrt::device(0);
    auto uuid = device.load_xclbin(binaryFile);
    auto krnl = xrt::kernel(device, uuid, "krnl_hash_dna");

    std::vector<uint64_t> input((sequence_length + 7) / 8, 0);
    std::vector<uint64_t> output(n_sm, 0);

    const std::string dna = "ACAAGGTCTGGTGATTCGCGACCTGCCGCTGATTGCCAGCAACTTCCGTAATACCGAAGACCTCTCTTCTTACCTGAAACGCCATAACATCGTGGCGATT"; 
    for (size_t i = 0; i < dna.size(); ++i) {
        input[i / 8] |= ((uint64_t)dna[i] & 0xFF) << (8 * (i % 8));
    }

    auto bo_input = xrt::bo(device, input.size() * sizeof(uint64_t), krnl.group_id(0));
    auto bo_output = xrt::bo(device, output.size() * sizeof(uint64_t), krnl.group_id(1));

    bo_input.write(input.data());
    bo_output.write(output.data());

    bo_input.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_output.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto start_time = std::chrono::high_resolution_clock::now();

    auto run = krnl(bo_input, bo_output, sequence_length);
    run.wait();

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_us = std::chrono::duration<double, std::micro>(end_time - start_time).count();

    bo_output.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_output.read(output.data());

    std::cout << std::dec;
    std::cout << "\n Kernel execution time: " << duration_us << " us (" << duration_us / 1000.0 << " ms)" << std::endl;

    double bytes_processed = sequence_length * sizeof(uint8_t);
    double throughput = (bytes_processed / duration_us) * 1e6 / (1024 * 1024 * 1024);

    std::cout << " Approx. throughput: " << throughput << " GB/s" << std::endl;

    return 0;
}
