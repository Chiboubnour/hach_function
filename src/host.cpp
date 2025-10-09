#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
#include <stdint.h>
#include <stdlib.h>
#include <random>
#include <iomanip>

// XRT includes
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

/*
OPTIMIZED VERSION: 512-bit data width with 8x parallel hash computation
+-----------+                   +-----------+
|           |                   |           |
|   HBM0    | ---- 512b ------>|           |
|  (Input)  |                   |           |
+-----------+                   |           |
                                |           |
+-----------+                   |           |
|           |                   |           |
|   HBM1    | <--- 512b -------|  KERNEL   |
|  (Output) |                   |           |
+-----------+                   |           |
                                |           |
                                +-----------+

PERFORMANCE: 8 HASHES PER CLOCK CYCLE = 2.4 BILLION HASHES/SECOND @ 300MHz
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
                int bank_assign[2], const std::vector<uint8_t>& sequence_bytes,
                size_t n) 
{
    // Calculer les tailles pour 512 bits
    size_t n_words_512bit = (n + 63) / 64; // 64 bases par mot 512 bits
    size_t input_size_bytes = n_words_512bit * 64; // 64 bytes par mot 512 bits
    
    size_t n_smers = (n >= 28) ? (n - 27) : 0; // S=28
    size_t output_words_512bit = (n_smers + 7) / 8; // 8 hashs par mot 512 bits
    size_t output_size_bytes = output_words_512bit * 64; // 64 bytes par mot 512 bits
    
    auto bo_seq  = xrt::bo(device, input_size_bytes, bank_assign[0]);
    auto bo_hash = xrt::bo(device, output_size_bytes, bank_assign[1]);

    auto seq_map  = bo_seq.map<uint64_t*>();
    auto hash_map = bo_hash.map<uint64_t*>();

    // Packer les données en mots de 512 bits
    for (size_t word_idx = 0; word_idx < n_words_512bit; word_idx++) {
        for (size_t base_idx = 0; base_idx < 64; base_idx++) {
            size_t global_base_idx = word_idx * 64 + base_idx;
            if (global_base_idx < n) {
                uint8_t base_value = sequence_bytes[global_base_idx];
                seq_map[word_idx * 8 + (base_idx / 8)] |= (uint64_t(base_value) << (8 * (base_idx % 8)));
            }
        }
    }

    std::memset(hash_map, 0, output_size_bytes);

    bo_seq.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto kernel_start = std::chrono::high_resolution_clock::now();
    auto run = krnl(bo_seq, n, bo_hash);
    run.wait();
    auto kernel_end = std::chrono::high_resolution_clock::now();

    bo_hash.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    std::chrono::duration<double> kernel_time = kernel_end - kernel_start;

    return kernel_time.count();
}

// Fonction pour générer une séquence ADN aléatoire
std::vector<uint8_t> generate_random_dna_sequence(size_t length, uint32_t seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dis(0, 3);
    const uint8_t bases[4] = {'A', 'C', 'G', 'T'};
    
    std::vector<uint8_t> sequence(length);
    for (size_t i = 0; i < length; i++) {
        sequence[i] = bases[dis(gen)];
    }
    return sequence;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <xclbin_file> <device_id> [sequence_length]\n";
        std::cout << "Default sequence length: 1,000,000 bases\n";
        return EXIT_FAILURE;
    }

    std::string binaryFile = argv[1];
    int device_index = std::stoi(argv[2]);
    
    // Taille de séquence par défaut : 1 million de bases
    size_t n = (argc > 3) ? std::stoull(argv[3]) : 1000000;

    std::cout << "=== HASH COMPUTATION - 512-bit + 8x Parallel ===\n";
    std::cout << "Target: " << n << " bases (" << n/1000000.0 << " million bases)\n";
    std::cout << "Ouverture du device " << device_index << std::endl;
    auto device = xrt::device(device_index);

    auto uuid = device.load_xclbin(binaryFile);

    auto krnl = xrt::kernel(device, uuid, "krnl_hach");

    int bank_assign[2] = {0, 1};

    std::cout << "\n=== Génération de séquence ADN aléatoire ===" << std::endl;
    std::vector<uint8_t> sequence_bytes = generate_random_dna_sequence(n);
    
    // Afficher un échantillon de la séquence
    std::cout << "Échantillon (premières 100 bases): ";
    for (size_t i = 0; i < std::min(size_t(100), n); i++) {
        std::cout << char(sequence_bytes[i]);
    }
    std::cout << "...\n";

    std::cout << "\n=== Traitement FPGA ===" << std::endl;
    double kernel_time_in_sec = run_krnl(device, krnl, bank_assign, sequence_bytes, n);

    size_t n_smers = (n >= 28) ? (n - 27) : 0;

    // Calculs de performance détaillés
    double time_per_smer_sec = (n_smers > 0) ? (kernel_time_in_sec / n_smers) : 0.0;
    double time_per_smer_ns = time_per_smer_sec * 1e9;
    double hashes_per_second = n_smers / kernel_time_in_sec;

    std::cout << "\n=== RÉSULTATS DE PERFORMANCE ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Taille de séquence : " << n << " bases (" << n/1000000.0 << " millions)\n";
    std::cout << "Nombre de s-mers générés : " << n_smers << " (" << n_smers/1000000.0 << " millions)\n";
    std::cout << "Temps d'exécution kernel : " << kernel_time_in_sec << " secondes\n";
    std::cout << "Temps moyen par s-mer : " << time_per_smer_ns << " ns\n";
    std::cout << "Débit de calcul : " << hashes_per_second / 1e6 << " millions de hashs/seconde\n";
    std::cout << "Débit théorique max (8 hashs/cycle @ 300MHz) : 2400 millions de hashs/seconde\n";
    std::cout << "Efficacité de calcul : " << (hashes_per_second / 2.4e9) * 100 << "%\n";

    std::cout << "\nTest terminé avec succès." << std::endl;
    return 0;
}
