# Hash Computation - 512-bit + 8x Parallel

Accélérateur FPGA optimisé pour le calcul de hash de séquences ADN sur Alveo U280.

## Caractéristiques

- **512 bits** de largeur de données (entrée/sortie)
- **8 hachages par cycle d'horloge** (traitement parallèle)
- **2.4 milliards de hashs/seconde** @ 300MHz (performance théorique)
- **Mémoire HBM** pour haute bande passante
- **Séquences aléatoires** jusqu'à plusieurs millions de bases

## Architecture

```
Input (512-bit) → Unpack → Generate s-mers → Compute 8x Hashes → Store (512-bit)
```

## Compilation

```bash
make all TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

## Exécution

```bash
# 1 million de bases (défaut)
./host.exe krnl_hach.hw.xclbin 0

# Taille personnalisée
./host.exe krnl_hach.hw.xclbin 0 5000000  # 5 millions de bases
```

## Test de performance

```bash
./test_performance.sh krnl_hach.hw.xclbin 0
```

```
