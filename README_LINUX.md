# Hash DNA Kernel - Alveo U280

## Problèmes corrigés

✅ **Fichier de configuration manquant** : `krnl_hash.cfg` créé avec la bonne configuration
✅ **Incohérence de noms** : Tous les noms de kernel sont maintenant cohérents (`krnl_hash_dna`)
✅ **Problème de plateforme** : U280 retiré de la blocklist
✅ **Interfaces HLS** : Corrigées dans le kernel
✅ **Makefile simplifié** : `Makefile_simple.mk` créé pour éviter les problèmes

## Installation et configuration

### 1. Prérequis
```bash
# Installer Vitis 2022.2
# Installer XRT
# Installer la plateforme U280
```

### 2. Configuration de l'environnement
```bash
# Sourcez Vitis
source /tools/Xilinx/Vitis/2022.2/settings64.sh

# Sourcez XRT
source /opt/xilinx/xrt/setup.sh

# Vérifiez que les variables sont définies
echo $XILINX_VITIS
echo $XILINX_XRT
```

### 3. Vérification de l'environnement
```bash
# Rendez le script exécutable
chmod +x check_setup.sh

# Exécutez la vérification
./check_setup.sh
```

## Utilisation

### Test rapide
```bash
# Test complet (recommandé en premier)
chmod +x test_linux.sh
./test_linux.sh
```

### Build et exécution

#### Build hardware
```bash
make -f Makefile_simple.mk TARGET=hw build
```

#### Build emulation (plus rapide pour les tests)
```bash
make -f Makefile_simple.mk TARGET=hw_emu build
```

#### Exécution
```bash
# Sur la carte (après build hardware)
make -f Makefile_simple.mk TARGET=hw run

# En emulation (après build emulation)
make -f Makefile_simple.mk TARGET=hw_emu run
```

### Nettoyage
```bash
# Nettoyage simple
make -f Makefile_simple.mk clean

# Nettoyage complet
make -f Makefile_simple.mk cleanall
```

## Structure des fichiers

```
hach_function-main/
├── src/
│   ├── krnl_hash.cpp      # Kernel HLS
│   └── host.cpp           # Code host
├── krnl_hash.cfg          # Configuration kernel
├── Makefile_simple.mk     # Makefile simplifié
├── check_setup.sh         # Script de vérification
├── test_linux.sh          # Script de test rapide
└── README_LINUX.md        # Ce fichier
```

## Dépannage

### Erreur de plateforme
Si vous obtenez une erreur de plateforme non supportée :
- Vérifiez que la plateforme U280 est installée
- Vérifiez le chemin dans `$XILINX_VITIS/platforms/`

### Erreur de compilation kernel
- Vérifiez que `krnl_hash.cfg` existe
- Vérifiez que les noms de kernel sont cohérents
- Essayez d'abord avec `TARGET=hw_emu`

### Erreur d'exécution
- Vérifiez que la carte est détectée : `xbutil list`
- Vérifiez les permissions : `sudo chmod 666 /dev/xclmgmt*`

## Performance attendue

- **Temps de build hardware** : 30-60 minutes
- **Temps de build emulation** : 5-10 minutes
- **Throughput** : ~1-5 GB/s selon la configuration

## Support

En cas de problème, vérifiez :
1. Les logs de compilation dans `build_dir.*/`
2. Les logs d'exécution avec `XRT_VERBOSE=1`
3. Le statut de la carte avec `xbutil examine` 