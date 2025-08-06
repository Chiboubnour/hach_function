# 🧬 Explication Détaillée de l'Accélérateur DNA Hash

## 🎯 **Objectif de l'Accélérateur**

Votre accélérateur calcule des **hashes de k-mers** à partir d'une séquence d'ADN. Un k-mer est une sous-séquence de k nucléotides consécutifs.

**Exemple :** Pour la séquence "ACGT" avec k=2, les k-mers sont : "AC", "CG", "GT"

## 📊 **Données d'Entrée**

### Séquence d'ADN utilisée :
```
ACAAGGTCTGGTGATTCGCGACCTGCCGCTGATTGCCAGCAACTTCCGTAATACCGAAGACCTCTCTTCTTACCTGAAACGCCATAACATCGTGGCGATT
```

### Paramètres :
- **k = 28** (longueur des k-mers)
- **Séquence = 100 bases** (par défaut)
- **Nombre de k-mers = 100 - 28 + 1 = 73**

## 🔄 **Pipeline de Traitement (4 Étapes)**

### 1️⃣ **unpack_sequence** - Décompression des données
```cpp
// Entrée : Données packées (8 bases par mot 64-bit)
// Sortie : Stream de bases individuelles (2 bits par base)
```

**Encodage des nucléotides :**
- A = 00 (0)
- C = 01 (1) 
- G = 10 (2)
- T = 11 (3)

### 2️⃣ **generate_smers** - Génération des k-mers
```cpp
// Entrée : Stream de bases (2 bits)
// Sortie : Stream de k-mers (56 bits pour 28 bases)
```

**Exemple de génération :**
```
Séquence : ACGTACGT...
k-mer 1 : ACGTACGT... (28 bases)
k-mer 2 : CGTACGTA... (28 bases)
...
```

### 3️⃣ **compute_hashes** - Calcul des hashes
```cpp
// Entrée : Stream de k-mers
// Sortie : Stream de hashes (64 bits)
```

**Algorithme de hash (BFC - Bob Jenkins hash) :**
```cpp
key = (~key + (key << 21)) & mask;
key = key ^ (key >> 24);
key = ((key + (key << 3)) + (key << 8)) & mask;
key = key ^ (key >> 14);
key = ((key + (key << 2)) + (key << 4)) & mask;
key = key ^ (key >> 28);
key = (key + (key << 31)) & mask;
```

### 4️⃣ **store_hashes** - Stockage des résultats
```cpp
// Entrée : Stream de hashes
// Sortie : Tableau de hashes en mémoire
```

## 📈 **Résultats Attendus**

### Pour la séquence de test (100 bases) :
- **Nombre de k-mers générés :** 73
- **Nombre de hashes calculés :** 73
- **Temps d'exécution attendu :** < 1 ms
- **Throughput attendu :** > 1 GB/s

### Format de sortie :
```cpp
// Tableau de 73 valeurs uint64_t
output[0] = hash du k-mer 1 (bases 0-27)
output[1] = hash du k-mer 2 (bases 1-28)
...
output[72] = hash du k-mer 73 (bases 72-99)
```

## 🔍 **Comment vérifier que ça fonctionne**

### 1. Vérification des logs :
```bash
# Exécution avec logs détaillés
XRT_VERBOSE=1 ./host krnl_hash_dna.xclbin 100
```

### 2. Résultats attendus :
```
Kernel execution time: XXX us (X.XXX ms)
Approx. throughput: X.XXX GB/s
```

### 3. Vérification des hashes :
- Tous les hashes doivent être différents (probabilité très élevée)
- Les hashes doivent être des valeurs 64-bit valides
- Pas de valeurs nulles ou aberrantes

## 🚀 **Optimisations HLS**

### Pipeline II=1 :
- Chaque étape traite une donnée par cycle d'horloge
- Pipeline parallèle entre les 4 étapes

### Dataflow :
- Les 4 étapes s'exécutent en parallèle
- Communication via streams HLS

### Optimisations mémoire :
- Utilisation de 2 banques DDR séparées
- Accès optimisés aux buffers

## 🐛 **Dépannage**

### Si les temps sont trop lents :
- Vérifiez la fréquence d'horloge
- Vérifiez les contraintes de timing

### Si les hashes sont identiques :
- Problème dans l'algorithme de hash
- Vérifiez les masques

### Si crash ou erreur :
- Vérifiez la taille des buffers
- Vérifiez les permissions de la carte 