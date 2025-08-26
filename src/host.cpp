#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
#include <stdint.h>
#include <stdlib.h>

// XRT includes
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

/*
CASE: Data flow for krnl_hach kernel on FPGA with HBM banks
+-----------+                   +-----------+
|           |                   |           |
|   HBM0    | ---- Sequence --->|           |
|  (Input)  |                   |           |
+-----------+                   |           |
                                |           |
+-----------+                   |           |
|           |                   |           |
|   HBM1    | <--- Hashes ------|   KERNEL  |
|  (Output) |                   |           |
+-----------+                   |           |
                                |           |
                                +-----------+


+------------+       +---------------------+       +----------------+       +----------------+       +------------+
|            |       |                     |       |                |       |                |       |            |
|   HBM0     | ----> | Unpack Sequence     | ----> | Generate s-mers | ----> | Compute Hashes | ----> | Store Hashes|
| (Input)    |       | (64b packed -> 2b)  |       | (Sliding window)|       | (Hash function)|       |            |
+------------+       +---------------------+       +----------------+       +----------------+       +------------+
                                                                                                          |
                                                                                                          v
                                                                                                    +------------+
                                                                                                    |   HBM1     |
                                                                                                    | (Output)   |
                                                                                                    +------------+

*/

inline uint64_t nucl_encode(char c) {
    switch(c) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default:  return 0;
    }
}

double run_krnl(xrt::device& device, xrt::kernel& krnl,
                int bank_assign[2], const std::vector<uint64_t>& packed_seq,
                size_t n) 
{
    size_t input_size_bytes = packed_seq.size() * sizeof(uint64_t);
    size_t n_smers = (n >= 28) ? (n - 27) : 0; // S=28
    size_t output_size_bytes = n_smers * sizeof(uint64_t);

    std::cout << "Allocation des buffers en mémoire globale...\n";
    auto bo_seq  = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);

    auto seq_map  = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();

    // Copier la séquence packée dans le buffer
    for (size_t i = 0; i < packed_seq.size(); i++)
        seq_map[i] = packed_seq[i];

    std::memset(hash_map, 0, output_size_bytes);

    std::cout << "Synchronisation buffer entrée vers device...\n";
    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    std::cout << "Lancement du kernel...\n";
    auto kernel_start = std::chrono::high_resolution_clock::now();
    auto run = krnl(bo_seq, n, bo_hash);
    run.wait();
    auto kernel_end = std::chrono::high_resolution_clock::now();

    bo_hash.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    std::chrono::duration<double> kernel_time = kernel_end - kernel_start;

    return kernel_time.count();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <xclbin_file> <device_id>\n";
        return EXIT_FAILURE;
    }

    std::string binaryFile = argv[1];
    int device_index = std::stoi(argv[2]);

    std::cout << "Ouverture du device " << device_index << std::endl;
    auto device = xrt::device(device_index);

    std::cout << "Chargement du fichier xclbin : " << binaryFile << std::endl;
    auto uuid = device.load_xclbin(binaryFile);

    auto krnl = xrt::kernel(device, uuid, "krnl_hach");

    int bank_assign[2] = {0, 1}; // banques HBM pour entrée et sortie
    const char bases[4] = {'A','C','G','T'};

    // ====  n = 512 ====
    size_t n = 512;
    std::cout << "\n=== Test avec n=" << n << " bases ===" << std::endl;

    std::vector<uint8_t> sequence_bytes(n);
    for (size_t i = 0; i < n; i++)
        sequence_bytes[i] = bases[i % 4];

    //  8 octets ASCII par mot (8 bases/mot)
    size_t n_words = (n + 7) / 8; // 8 bases par uint64_t
    std::vector<uint64_t> packed_seq(n_words, 0);

    for (size_t i = 0; i < n; i++) {
        size_t word_idx = i / 8;
        size_t shift = 8 * (i % 8);
        packed_seq[word_idx] |= static_cast<uint64_t>(sequence_bytes[i]) << shift;
    }

    double kernel_time_in_sec = run_krnl(device, krnl, bank_assign, packed_seq, n);

    size_t n_smers = (n >= 28) ? (n - 27) : 0;
    double total_bytes = (packed_seq.size() * sizeof(uint64_t)) + (n_smers * sizeof(uint64_t));
    double total_gb = total_bytes / 1e9;

    std::cout << "Temps d'exécution du kernel : " << kernel_time_in_sec << " s\n";
    std::cout << "Consommation : " << total_gb << " GB\n";

    std::cout << "\nTest terminé avec succès." << std::endl;

    return 0;
}
