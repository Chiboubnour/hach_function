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

// Fonction qui lance le kernel
double run_krnl(xrtDeviceHandle device, xrt::kernel& krnl, int bank_assign[2], unsigned int n) {
    size_t input_size_bytes = ((n + 7) / 8) * sizeof(uint64_t); // taille séquence encodée
    size_t n_smers = n - 27; // S=28, donc nombre de s-mers = n-(S-1)
    size_t output_size_bytes = n_smers * sizeof(uint64_t);

    std::cout << "Allocation des buffers en mémoire globale...\n";
    auto bo_seq = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);

    auto seq_map = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();

    // Initialisation buffer d'entrée avec un exemple de séquence
    std::cout << "Initialisation du buffer de séquence...\n";
    for (size_t i = 0; i < input_size_bytes / sizeof(uint64_t); i++) {
        seq_map[i] = 0x4141414141414141ULL; // 'A' ASCII répété
    }
    // Mise à zéro du buffer de sortie
    std::memset(hash_map, 0, output_size_bytes);

    std::cout << "Synchronisation buffer entrée vers device...\n";
    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    std::chrono::duration<double> kernel_time(0);

    std::cout << "Lancement du kernel...\n";
    auto kernel_start = std::chrono::high_resolution_clock::now();

    auto run = krnl(bo_seq, n, bo_hash);
    run.wait();

    auto kernel_end = std::chrono::high_resolution_clock::now();
    kernel_time = kernel_end - kernel_start;

    std::cout << "Synchronisation buffer sortie depuis device...\n";
    bo_hash.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    std::cout << "Premiers 10 hash générés:\n";
    for (size_t i = 0; i < std::min<size_t>(10, n_smers); i++) {
        std::cout << std::hex << hash_map[i] << std::dec << std::endl;
    }

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

    // Le nom du kernel doit correspondre à celui défini dans le kernel (ici krnl_hach)
    auto krnl = xrt::kernel(device, uuid, "krnl_hach");

    const unsigned int n = 1024;  // taille de la séquence test (nombre de bases)

    int bank_assign[2] = {0, 1};  // banque mémoire pour entrée et sortie

    double kernel_time_in_sec = run_krnl(device, krnl, bank_assign, n);

    size_t n_smers = n - 27;
    double total_bytes = (((n + 7)/8) + n_smers) * sizeof(uint64_t);

    double throughput = total_bytes / kernel_time_in_sec / 1e9; // en GB/s

    std::cout << "Temps d'exécution du kernel : " << kernel_time_in_sec << " s\n";
    std::cout << "Débit : " << throughput << " GB/s\n";

    std::cout << "Test terminé avec succès." << std::endl;

    return 0;
}