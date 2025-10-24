# Hash Computation - 512-bit + 8x Parallel

Accélérateur FPGA optimisé pour le calcul de hash de séquences ADN sur Alveo U280.

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
