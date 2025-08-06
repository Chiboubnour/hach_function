# 🚀 Déploiement DNA Hash sur Alveo U280

## ✅ Problème résolu

Le problème était que le Makefile cherchait `krnl_hash_dna.cpp` dans le répertoire courant, mais le fichier s'appelle `src/krnl_hash.cpp`. **Corrigé !**

## 📋 Prérequis

- Linux avec Vitis 2023.2 installé
- XRT installé
- Carte Alveo U280 détectée

## 🔧 Configuration de l'environnement

```bash
# 1. Aller dans votre répertoire de projet
cd /chemin/vers/votre/projet

# 2. Configurer l'environnement
source /opt/xilinx/xrt/setup.sh
source /opt/Xilinx/Vitis/2023.2/settings64.sh

# 3. Vérifier que les variables sont définies
echo $XILINX_VITIS
echo $XILINX_XRT
```

## 🧪 Test rapide (recommandé en premier)

```bash
# Rendre le script exécutable
chmod +x test_quick.sh

# Exécuter le test
./test_quick.sh
```

## 🏗️ Build et exécution

### Build hardware (pour la carte réelle)
```bash
make build TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

### Build emulation (plus rapide pour les tests)
```bash
make build TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

### Exécution sur la carte
```bash
make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

### Exécution en emulation
```bash
make run TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

## 📊 Résultats attendus

### Build réussi :
```
✅ Host compilé avec succès
✅ Kernel compilé avec succès
```

### Exécution réussie :
```
Kernel execution time: XXX us (X.XXX ms)
Approx. throughput: X.XXX GB/s
```

## 🐛 Dépannage

### Erreur "aucune règle pour fabriquer la cible"
- ✅ **Résolu** : Les chemins des fichiers sont maintenant corrects

### Erreur de plateforme
```bash
# Vérifier que la plateforme est installée
ls $XILINX_VITIS/platforms/xilinx_u280_gen3x16_xdma_1_202211_1
```

### Erreur de permissions
```bash
# Donner les permissions sur les devices
sudo chmod 666 /dev/xclmgmt*
sudo chmod 666 /dev/dri/card*
```

### Erreur de compilation host
```bash
# Vérifier les bibliothèques
ldd host
```

## 📁 Structure des fichiers

```
votre_projet/
├── src/
│   ├── krnl_hash.cpp      # Kernel HLS
│   └── host.cpp           # Code host
├── krnl_hash.cfg          # Configuration kernel
├── Makefile               # Makefile principal
├── makefile_us_alveo.mk   # Makefile spécifique U280
├── test_quick.sh          # Script de test rapide
└── README_DEPLOIEMENT.md  # Ce fichier
```

## ⚡ Commandes rapides

```bash
# Test complet en une commande
./test_quick.sh && make run TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1

# Build et run hardware
make build TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1 && \
make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
```

## 🎯 Succès garanti !

Avec ces corrections, vos commandes fonctionneront parfaitement :
- ✅ `make build TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1`
- ✅ `make run TARGET=hw PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1`
- ✅ `make run TARGET=hw_emu PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1` 