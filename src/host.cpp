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
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>

// Encodeur nucléotides sur 2 bits
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
    size_t n) {

    size_t input_size_bytes = packed_seq.size() * sizeof(uint64_t);
    size_t n_smers = n - 27; // S=28
    size_t output_size_bytes = n_smers * sizeof(uint64_t);

    std::cout << "Allocation des buffers en mémoire globale...\n";
    auto bo_seq  = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);

    auto seq_map  = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();

    // Copier la séquence packée dans le buffer
    for (size_t i = 0; i < packed_seq.size(); i++)
        seq_map[i] = packed_seq[i];

    // Zéro le buffer de sortie
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

    // Afficher premiers hash
    std::cout << "Premiers 10 hash générés:\n";
    for (size_t i = 0; i < std::min<size_t>(10, n_smers); i++)
        std::cout << std::hex << hash_map[i] << std::dec << std::endl;

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

    // Génération d'une séquence de 100 bases alternées ACGT
    const unsigned int n = 100;
    std::vector<uint8_t> sequence_bytes(n);
    const char bases[4] = {'A','C','G','T'};
    for (unsigned int i = 0; i < n; i++)
        sequence_bytes[i] = bases[i % 4];

    // Packager la séquence dans uint64_t, 2 bits par base
    size_t n_words = (n + 31) / 32; // 32 bases par uint64_t
    std::vector<uint64_t> packed_seq(n_words, 0);

    for (size_t i = 0; i < n; i++) {
        size_t word_idx = i / 32;
        size_t shift = 2 * (i % 32);
        packed_seq[word_idx] |= nucl_encode(sequence_bytes[i]) << shift;
    }

    int bank_assign[2] = {0, 1}; // banques HBM pour entrée et sortie
    double kernel_time_in_sec = run_krnl(device, krnl, bank_assign, packed_seq, n);

    size_t n_smers = n - 27;
    double total_bytes = (packed_seq.size() + n_smers) * sizeof(uint64_t);
    double throughput = total_bytes / kernel_time_in_sec / 1e9; // GB/s

    std::cout << "Temps d'exécution du kernel : " << kernel_time_in_sec << " s\n";
    std::cout << "Débit : " << throughput << " GB/s\n";
    std::cout << "Test terminé avec succès." << std::endl;

    return 0;
}
