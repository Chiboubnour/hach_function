#!/bin/bash

echo "=== Test rapide pour Linux ==="

# Vérifier l'environnement
echo "1. Vérification de l'environnement..."
if [ -z "$XILINX_VITIS" ]; then
    echo "❌ XILINX_VITIS non défini"
    echo "   Sourcez: source /tools/Xilinx/Vitis/2022.2/settings64.sh"
    exit 1
fi

if [ -z "$XILINX_XRT" ]; then
    echo "❌ XILINX_XRT non défini"
    echo "   Sourcez: source /opt/xilinx/xrt/setup.sh"
    exit 1
fi

echo "✅ Environnement OK"

# Nettoyer les builds précédents
echo "2. Nettoyage..."
make -f Makefile_simple.mk clean

# Test de compilation du host
echo "3. Test compilation host..."
make -f Makefile_simple.mk host
if [ $? -eq 0 ]; then
    echo "✅ Host compilé avec succès"
else
    echo "❌ Erreur compilation host"
    exit 1
fi

# Test de compilation kernel (hw_emu pour aller plus vite)
echo "4. Test compilation kernel (hw_emu)..."
make -f Makefile_simple.mk TARGET=hw_emu build
if [ $? -eq 0 ]; then
    echo "✅ Kernel compilé avec succès"
else
    echo "❌ Erreur compilation kernel"
    exit 1
fi

echo "=== Test terminé avec succès ==="
echo "Vous pouvez maintenant faire:"
echo "  make -f Makefile_simple.mk TARGET=hw build  # Pour le build hardware"
echo "  make -f Makefile_simple.mk TARGET=hw run    # Pour exécuter sur la carte" 