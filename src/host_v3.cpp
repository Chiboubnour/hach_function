// host.cpp
#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

using namespace std;

vector<uint8_t> generate_random_dna_sequence(size_t length, uint32_t seed = 42) {
    mt19937 gen(seed);
    uniform_int_distribution<> dis(0, 3);
    const uint8_t bases[4] = {'A', 'C', 'G', 'T'};
    vector<uint8_t> seq(length);
    for (size_t i = 0; i < length; ++i) seq[i] = bases[dis(gen)];
    return seq;
}

double run_krnl(xrt::device& device, xrt::kernel& krnl,
                int bank_assign[2], const vector<uint8_t>& sequence_bytes,
                size_t n_bases, uint64_t& n_minimizers)
{
    size_t n_words_512 = (n_bases + 63) / 64;
    size_t input_size_bytes = n_words_512 * 64;

    size_t n_smers = (n_bases >= 28) ? (n_bases - 27) : 0;
    size_t out_words_512 = (n_smers + 7) / 8;
    size_t output_size_bytes = out_words_512 * 64;

    auto bo_seq  = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);
    auto bo_nmin = xrt::bo(device, sizeof(uint64_t), bank_assign[1]);

    auto seq_map  = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();
    auto nmin_map = bo_nmin.map<uint64_t*>();

    size_t seq_map_words = input_size_bytes / sizeof(uint64_t);
    for (size_t i = 0; i < seq_map_words; ++i) seq_map[i] = 0ULL;
    size_t hash_map_words = (output_size_bytes > 0) ? (output_size_bytes / sizeof(uint64_t)) : 0;
    for (size_t i = 0; i < hash_map_words; ++i) hash_map[i] = 0ULL;
    *nmin_map = 0ULL;

    for (size_t word_idx = 0; word_idx < n_words_512; ++word_idx) {
        for (size_t byte_idx = 0; byte_idx < 64; ++byte_idx) {
            size_t global_base_idx = word_idx * 64 + byte_idx;
            if (global_base_idx < n_bases) {
                uint8_t base_value = sequence_bytes[global_base_idx];
                size_t u64_index = word_idx * 8 + (byte_idx / 8);
                unsigned shift = 8 * (byte_idx % 8);
                seq_map[u64_index] |= (uint64_t(base_value) << shift);
            }
        }
    }

    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nmin.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto t0 = chrono::high_resolution_clock::now();

    auto run = krnl(bo_seq, (uint64_t)n_bases, bo_hash, bo_nmin);
    run.wait();

    auto t1 = chrono::high_resolution_clock::now();

    bo_hash.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_nmin.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    n_minimizers = *nmin_map;

    chrono::duration<double> elapsed = t1 - t0;
    return elapsed.count();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <xclbin> <device_index> [sequence_length]\n";
        return EXIT_FAILURE;
    }

    string xclbin = argv[1];
    int device_idx = stoi(argv[2]);
    size_t n = (argc > 3) ? stoull(argv[3]) : 1000000;

    cout << "Device index: " << device_idx << "\n";
    cout << "Sequence length: " << n << " bases\n";

    auto device = xrt::device(device_idx);
    auto uuid = device.load_xclbin(xclbin);
    auto krnl = xrt::kernel(device, uuid, "minimizer");

    int bank_assign[2] = {0, 1};

    cout << "Generating random DNA sequence...\n";
    vector<uint8_t> seq = generate_random_dna_sequence(n);

    cout << "Running kernel...\n";
    uint64_t n_minimizers = 0;
    double kernel_time = run_krnl(device, krnl, bank_assign, seq, n, n_minimizers);

    size_t n_smers = (n >= 28) ? (n - 27) : 0;
    double time_per_smer_ns = (n_smers > 0) ? (kernel_time / (double)n_smers * 1e9) : 0.0;
    double hashes_per_sec = (kernel_time > 0.0) ? (double)n_smers / kernel_time : 0.0;

    cout << fixed << setprecision(6);
    cout << "Kernel time (s): " << kernel_time << "\n";
    cout << "S-mers: " << n_smers << "\n";
    cout << "Time per s-mer: " << time_per_smer_ns << " ns\n";
    cout << "Hashes/s: " << hashes_per_sec << "\n";
    cout << "Minimizers produced: " << n_minimizers << "\n";

    cout << "Done.\n";
    return 0;
}
