# ============================================================
#  dev.ps1  —  Raccourcis de développement
#
#  .\dev.ps1 start          Démarre le conteneur + configure CMake
#  .\dev.ps1 build          Compilation incrémentale
#  .\dev.ps1 test           Tous les tests
#  .\dev.ps1 unit           Tests unitaires seulement
#  .\dev.ps1 integ          Tests d'intégration seulement
#  .\dev.ps1 watch <nom>    Relance un test précis en boucle
#  .\dev.ps1 shell          Shell bash interactif
#  .\dev.ps1 stop           Arrête le conteneur
# ============================================================

param([string]$cmd = "help", [string]$arg = "")

$COMPOSE = "docker compose -f docker-compose.dev.yml"
$EXEC    = "docker exec swing_bot_dev"

# Commande cmake commune (retire CMP0167 comme dans ton Dockerfile)
$CMAKE_CONFIGURE = @"
sed -i '/cmake_policy(SET CMP0167/d' CMakeLists.txt 2>/dev/null; \
cmake -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -G Ninja
"@

switch ($cmd) {

    "start" {
        Write-Host "Démarrage du conteneur dev..." -ForegroundColor Cyan
        Invoke-Expression "$COMPOSE up -d --build"
        Write-Host "Configuration CMake..." -ForegroundColor Yellow
        docker exec swing_bot_dev bash -c $CMAKE_CONFIGURE
        Write-Host ""
        Write-Host "Prêt !" -ForegroundColor Green
        Write-Host "  .\dev.ps1 build   → compiler"
        Write-Host "  .\dev.ps1 test    → lancer les tests"
    }

    "build" {
        Write-Host "Compilation incrémentale..." -ForegroundColor Cyan
        docker exec swing_bot_dev bash -c "cmake --build build -j`$(nproc)"
    }

    "test" {
        Write-Host "Tous les tests..." -ForegroundColor Cyan
        docker exec swing_bot_dev bash -c "cd build && ctest --output-on-failure -j4"
    }

    "unit" {
        Write-Host "Tests unitaires..." -ForegroundColor Cyan
        docker exec swing_bot_dev bash -c "cd build && ctest -R '^Unit\.' --output-on-failure -j4"
    }

    "integ" {
        Write-Host "Tests d'intégration..." -ForegroundColor Cyan
        docker exec swing_bot_dev bash -c "cd build && ctest -R '^Integration\.' --output-on-failure"
    }

    "watch" {
        if ($arg -eq "") {
            Write-Host "Usage: .\dev.ps1 watch NomDuTest" -ForegroundColor Red
            Write-Host "Ex:    .\dev.ps1 watch SwingStrategyUnit.FlatMarketReturnsHold"
            exit
        }
        Write-Host "Surveillance: $arg  (Ctrl+C pour arrêter)" -ForegroundColor Yellow
        while ($true) {
            docker exec swing_bot_dev bash -c "cmake --build build -j`$(nproc) --target unit_tests integration_tests 2>&1 | tail -3"
            docker exec swing_bot_dev bash -c "cd build && ctest -R '$arg' --output-on-failure"
            Start-Sleep -Seconds 1
        }
    }

    "shell" {
        docker exec -it swing_bot_dev bash
    }

    "stop" {
        Invoke-Expression "$COMPOSE down"
        Write-Host "Conteneur arrêté." -ForegroundColor Gray
    }

    default {
        Write-Host ""
        Write-Host "Usage: .\dev.ps1 <commande>" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "  start          Démarre et configure (une seule fois)"
        Write-Host "  build          Compile les changements"
        Write-Host "  test           Tous les tests"
        Write-Host "  unit           Tests unitaires"
        Write-Host "  integ          Tests d'intégration"
        Write-Host "  watch <nom>    Relance un test en boucle"
        Write-Host "  shell          Shell bash"
        Write-Host "  stop           Arrête le conteneur"
        Write-Host ""
    }
}
