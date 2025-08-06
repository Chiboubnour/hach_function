#!/bin/bash

echo "=== Test de Configuration ==="

# Vérifier que le fichier de config existe
if [ ! -f "krnl_hash.cfg" ]; then
    echo "❌ krnl_hash.cfg manquant"
    exit 1
fi

echo "✅ krnl_hash.cfg trouvé"

# Vérifier le contenu du fichier de config
echo "Contenu de krnl_hash.cfg :"
cat krnl_hash.cfg

echo ""
echo "=== Test de compilation rapide ==="

# Test de compilation du host
echo "1. Compilation host..."
make host
if [ $? -eq 0 ]; then
    echo "✅ Host compilé"
else
    echo "❌ Erreur compilation host"
    exit 1
fi

# Test de compilation kernel (hw_emu pour aller plus vite)
echo "2. Compilation kernel (hw_emu)..."
make build TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
if [ $? -eq 0 ]; then
    echo "✅ Kernel compilé"
    echo "✅ Configuration corrigée !"
else
    echo "❌ Erreur compilation kernel"
    exit 1
fi

echo ""
echo "🎯 Maintenant vous pouvez utiliser :"
echo "  make build TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1"
echo "  make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1" 