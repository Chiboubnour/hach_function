#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <cstdint>
#include <cstring>

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

static inline uint8_t encode2bit(uint8_t c) {
    switch (c) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default : return 0;
    }
}

static std::vector<uint8_t> generate_random_dna_sequence(size_t length, uint32_t seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> dis(0, 3);
    const uint8_t bases[4] = {'A','C','G','T'};
    std::vector<uint8_t> seq(length);
    for (size_t i = 0; i < length; ++i) seq[i] = bases[dis(gen)];
    return seq;
}

static void pack_2bit_512(const std::vector<uint8_t>& bases_ascii, uint8_t* dst_bytes) {
    std::memset(dst_bytes, 0, 0); 
    const size_t n = bases_ascii.size();
    const size_t words = (n + 255) / 256; 
    uint64_t* dst64 = reinterpret_cast<uint64_t*>(dst_bytes);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t code = encode2bit(bases_ascii[i]) & 0x3;
        const size_t word_idx = i / 256;
        const size_t pos_in_word = i % 256; 
        const size_t bitpos = pos_in_word * 2; // 2 bits per base
        const size_t qword_index = bitpos / 64; // 0..7
        const size_t bit_in_qword = bitpos % 64; // 0..63

        uint64_t mask = (uint64_t)code << bit_in_qword;
        dst64[word_idx * 8 + qword_index] |= mask;
        const size_t remaining = 64 - bit_in_qword;
        if (remaining < 2) {
            // Crosses 64-bit boundary
            const uint64_t carry = (uint64_t)code >> remaining; 
            dst64[word_idx * 8 + (qword_index + 1)] |= carry;
        }
    }
    // silence unused variable when n==0
    (void)words;
}

static double run_kernel_v3(xrt::device& device,
                            xrt::kernel& krnl,
                            int bank_assign[2],
                            const std::vector<uint8_t>& sequence_ascii,
                            uint64_t n_bases,
                            uint64_t& n_minimizers_out) {
    const uint64_t words512 = (n_bases + 255) / 256; 
    const size_t input_size_bytes = words512 * 64;  

    const uint64_t n_smers = (n_bases >= 28) ? (n_bases - 28 + 1) : 0;
    const uint64_t out_words512 = (n_smers + 7) / 8; // 8 hashes per 512-bit word
    const size_t output_size_bytes = out_words512 * 64;

    auto bo_seq  = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_out  = xrt::bo(device, output_size_bytes, bank_assign[1]);
    auto bo_cnt  = xrt::bo(device, sizeof(uint64_t), bank_assign[1]);

    auto seq_map = bo_seq.map<uint8_t*>();
    auto out_map = bo_out.map<uint8_t*>();
    auto cnt_map = bo_cnt.map<uint64_t*>();

    std::memset(seq_map, 0, input_size_bytes);
    std::memset(out_map, 0, output_size_bytes);
    *cnt_map = 0;

    pack_2bit_512(sequence_ascii, seq_map);

    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_cnt.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto t0 = std::chrono::high_resolution_clock::now();
    auto r = krnl(bo_seq, bo_out, n_bases, bo_cnt);
    r.wait();
    auto t1 = std::chrono::high_resolution_clock::now();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_cnt.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    n_minimizers_out = *cnt_map;
    std::chrono::duration<double> dt = t1 - t0;
    return dt.count();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <xclbin_file> <device_id> [sequence_length]\n";
        std::cout << "Default sequence length: 1,000,000 bases\n";
        return EXIT_FAILURE;
    }

    const std::string xclbin = argv[1];
    const int device_index = std::stoi(argv[2]);
    const uint64_t n = (argc > 3) ? std::stoull(argv[3]) : 1000000ULL;

    std::cout << "=== Minimizer V3 - 512b input (2-bit packed) + dedup window ===\n";
    std::cout << "Sequence length: " << n << " bases\n";

    auto device = xrt::device(device_index);
    auto uuid = device.load_xclbin(xclbin);
    // Kernel name from Krnl_hach_v3.cpp
    auto krnl = xrt::kernel(device, uuid, "Krnl_hach_v3");

    int bank_assign[2] = {0, 1};

    std::cout << "Generating random DNA sequence...\n";
    auto sequence = generate_random_dna_sequence(n);

    std::cout << "Running kernel...\n";
    uint64_t n_minimizers = 0;
    double secs = run_kernel_v3(device, krnl, bank_assign, sequence, n, n_minimizers);

    const uint64_t n_smers = (n >= 28) ? (n - 27) : 0;
    const double hashes_per_sec = (secs > 0.0) ? (double)n_smers / secs : 0.0;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Kernel time: " << secs << " s\n";
    std::cout << "Throughput: " << (hashes_per_sec/1e9) << " Ghash/s\n";
    std::cout << "Minimizers produced: " << n_minimizers << "\n";
    std::cout << "Done." << std::endl;
    return 0;
}
