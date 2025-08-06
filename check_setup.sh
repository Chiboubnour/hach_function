#!/bin/bash

echo "=== Vérification de l'environnement Alveo U280 ==="

# Vérifier les variables d'environnement
echo "1. Vérification des variables d'environnement..."

if [ -z "$XILINX_VITIS" ]; then
    echo "❌ XILINX_VITIS n'est pas défini"
    echo "   Sourcez le fichier settings64.sh de Vitis"
    exit 1
else
    echo "✅ XILINX_VITIS: $XILINX_VITIS"
fi

if [ -z "$XILINX_XRT" ]; then
    echo "❌ XILINX_XRT n'est pas défini"
    echo "   Sourcez le fichier setup.sh de XRT"
    exit 1
else
    echo "✅ XILINX_XRT: $XILINX_XRT"
fi

# Vérifier les outils
echo ""
echo "2. Vérification des outils..."

if ! command -v v++ &> /dev/null; then
    echo "❌ v++ n'est pas trouvé dans le PATH"
    exit 1
else
    echo "✅ v++ trouvé: $(which v++)"
fi

if ! command -v g++ &> /dev/null; then
    echo "❌ g++ n'est pas trouvé dans le PATH"
    exit 1
else
    echo "✅ g++ trouvé: $(which g++)"
fi

# Vérifier les fichiers du projet
echo ""
echo "3. Vérification des fichiers du projet..."

REQUIRED_FILES=(
    "src/krnl_hash.cpp"
    "src/host.cpp"
    "krnl_hash.cfg"
    "description.json"
    "Makefile_simple.mk"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "✅ $file existe"
    else
        echo "❌ $file manquant"
        exit 1
    fi
done

# Vérifier la plateforme
echo ""
echo "4. Vérification de la plateforme..."

PLATFORM="xilinx_u280_gen3x16_xdma_1_202211_1"
PLATFORM_PATH="$XILINX_VITIS/platforms/$PLATFORM"

if [ -d "$PLATFORM_PATH" ]; then
    echo "✅ Plateforme $PLATFORM trouvée"
else
    echo "❌ Plateforme $PLATFORM non trouvée dans $PLATFORM_PATH"
    echo "   Vérifiez que la plateforme est installée"
    exit 1
fi

# Vérifier les devices disponibles
echo ""
echo "5. Vérification des devices..."

if command -v xbutil &> /dev/null; then
    echo "Devices disponibles:"
    xbutil list
else
    echo "⚠️  xbutil non trouvé, impossible de vérifier les devices"
fi

echo ""
echo "=== Vérification terminée ==="
echo "Si toutes les vérifications sont passées, vous pouvez utiliser:"
echo "  make -f Makefile_simple.mk build"
echo "  make -f Makefile_simple.mk run" 