#!/bin/bash

echo "=== Test Rapide du Projet DNA Hash ==="

# Vérifier l'environnement
echo "1. Vérification de l'environnement..."
if [ -z "$XILINX_VITIS" ]; then
    echo "❌ XILINX_VITIS non défini"
    echo "   Sourcez: source /opt/Xilinx/Vitis/2023.2/settings64.sh"
    exit 1
fi

if [ -z "$XILINX_XRT" ]; then
    echo "❌ XILINX_XRT non défini"
    echo "   Sourcez: source /opt/xilinx/xrt/setup.sh"
    exit 1
fi

echo "✅ Environnement OK"

# Vérifier les fichiers
echo "2. Vérification des fichiers..."
if [ ! -f "src/krnl_hash.cpp" ]; then
    echo "❌ src/krnl_hash.cpp manquant"
    exit 1
fi

if [ ! -f "src/host.cpp" ]; then
    echo "❌ src/host.cpp manquant"
    exit 1
fi

if [ ! -f "krnl_hash.cfg" ]; then
    echo "❌ krnl_hash.cfg manquant"
    exit 1
fi

echo "✅ Fichiers OK"

# Test de compilation du host
echo "3. Test compilation host..."
make host
if [ $? -eq 0 ]; then
    echo "✅ Host compilé avec succès"
else
    echo "❌ Erreur compilation host"
    exit 1
fi

# Test de compilation kernel (hw_emu pour aller plus vite)
echo "4. Test compilation kernel (hw_emu)..."
make build TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
if [ $? -eq 0 ]; then
    echo "✅ Kernel compilé avec succès"
else
    echo "❌ Erreur compilation kernel"
    exit 1
fi

echo ""
echo "=== Test terminé avec succès ==="
echo "Vous pouvez maintenant utiliser:"
echo "  make build TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1"
echo "  make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1" 