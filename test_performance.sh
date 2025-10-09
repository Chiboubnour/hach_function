#!/bin/bash

# Script de test de performance pour différentes tailles de séquences
# Usage: ./test_performance.sh <xclbin_file> <device_id>

if [ $# -lt 2 ]; then
    echo "Usage: $0 <xclbin_file> <device_id>"
    echo "Example: $0 krnl_hach.hw.xclbin 0"
    exit 1
fi

XCLBIN_FILE=$1
DEVICE_ID=$2

echo "=== Test de performance - Hash Computation 512-bit + 8x Parallel ==="
echo "XCLBIN: $XCLBIN_FILE"
echo "Device: $DEVICE_ID"
echo ""

# Tailles de test (en bases)
SIZES=(1000 10000 100000 1000000 5000000 10000000)

for size in "${SIZES[@]}"; do
    echo "=========================================="
    echo "Test avec $size bases ($(echo "scale=2; $size/1000000" | bc) millions)"
    echo "=========================================="
    
    ./host.exe $XCLBIN_FILE $DEVICE_ID $size
    echo ""
done

echo "=== Tests terminés ==="
