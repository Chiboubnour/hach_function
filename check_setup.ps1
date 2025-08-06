Write-Host "=== Vérification de l'environnement Alveo U280 ===" -ForegroundColor Green

# Vérifier les variables d'environnement
Write-Host "`n1. Vérification des variables d'environnement..." -ForegroundColor Yellow

if (-not $env:XILINX_VITIS) {
    Write-Host "❌ XILINX_VITIS n'est pas défini" -ForegroundColor Red
    Write-Host "   Sourcez le fichier settings64.sh de Vitis" -ForegroundColor Red
    exit 1
} else {
    Write-Host "✅ XILINX_VITIS: $env:XILINX_VITIS" -ForegroundColor Green
}

if (-not $env:XILINX_XRT) {
    Write-Host "❌ XILINX_XRT n'est pas défini" -ForegroundColor Red
    Write-Host "   Sourcez le fichier setup.sh de XRT" -ForegroundColor Red
    exit 1
} else {
    Write-Host "✅ XILINX_XRT: $env:XILINX_XRT" -ForegroundColor Green
}

# Vérifier les outils
Write-Host "`n2. Vérification des outils..." -ForegroundColor Yellow

try {
    $vppPath = Get-Command v++ -ErrorAction Stop
    Write-Host "✅ v++ trouvé: $($vppPath.Source)" -ForegroundColor Green
} catch {
    Write-Host "❌ v++ n'est pas trouvé dans le PATH" -ForegroundColor Red
    exit 1
}

try {
    $gppPath = Get-Command g++ -ErrorAction Stop
    Write-Host "✅ g++ trouvé: $($gppPath.Source)" -ForegroundColor Green
} catch {
    Write-Host "❌ g++ n'est pas trouvé dans le PATH" -ForegroundColor Red
    exit 1
}

# Vérifier les fichiers du projet
Write-Host "`n3. Vérification des fichiers du projet..." -ForegroundColor Yellow

$requiredFiles = @(
    "src\krnl_hash.cpp",
    "src\host.cpp",
    "krnl_hash.cfg",
    "description.json",
    "Makefile_simple.mk"
)

foreach ($file in $requiredFiles) {
    if (Test-Path $file) {
        Write-Host "✅ $file existe" -ForegroundColor Green
    } else {
        Write-Host "❌ $file manquant" -ForegroundColor Red
        exit 1
    }
}

# Vérifier la plateforme
Write-Host "`n4. Vérification de la plateforme..." -ForegroundColor Yellow

$platform = "xilinx_u280_gen3x16_xdma_1_202211_1"
$platformPath = Join-Path $env:XILINX_VITIS "platforms\$platform"

if (Test-Path $platformPath) {
    Write-Host "✅ Plateforme $platform trouvée" -ForegroundColor Green
} else {
    Write-Host "❌ Plateforme $platform non trouvée dans $platformPath" -ForegroundColor Red
    Write-Host "   Vérifiez que la plateforme est installée" -ForegroundColor Red
    exit 1
}

# Vérifier les devices disponibles
Write-Host "`n5. Vérification des devices..." -ForegroundColor Yellow

try {
    $xbutilPath = Get-Command xbutil -ErrorAction Stop
    Write-Host "Devices disponibles:" -ForegroundColor Cyan
    xbutil list
} catch {
    Write-Host "⚠️  xbutil non trouvé, impossible de vérifier les devices" -ForegroundColor Yellow
}

Write-Host "`n=== Vérification terminée ===" -ForegroundColor Green
Write-Host "Si toutes les vérifications sont passées, vous pouvez utiliser:" -ForegroundColor Cyan
Write-Host "  make -f Makefile_simple.mk build" -ForegroundColor White
Write-Host "  make -f Makefile_simple.mk run" -ForegroundColor White 