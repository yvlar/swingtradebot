#pragma once
// ============================================================
//  TempPath.hpp — noms de fichiers temporaires UNIQUES pour les
//  fixtures de test (D59).
//
//  Contexte : gtest_discover_tests exécute CHAQUE test dans un
//  processus ctest séparé, en parallèle. Un nom de fichier dérivé du
//  SEUL horodatage steady_clock (l'ancien motif des fixtures) peut
//  entrer en collision entre deux processus démarrés dans le même
//  tick d'horloge : les deux ouvrent alors le MÊME fichier SQLite →
//  « SQLite exec: database is locked » dans SetUp() (flake observé
//  en CI, run 29094492121, D59).
// ============================================================

#include <atomic>
#include <chrono>
#include <string>
#include <unistd.h>

namespace swingbot_test {

// Version « pure » vis-à-vis de l'horloge : le tick est un PARAMÈTRE,
// ce qui rend l'unicité à tick IDENTIQUE (le scénario de collision D59)
// testable de façon déterministe par TempPathUnit.
//
// Unicité garantie sur DEUX axes indépendants de l'horloge :
//  - le PID sépare deux processus ctest parallèles nés dans le même tick
//    (la cause racine D59) ;
//  - le compteur atomique sépare deux appels du même processus.
// Le tick reste dans le nom pour éviter toute réutilisation entre deux
// runs successifs (les PID se recyclent, un TearDown interrompu peut
// laisser un fichier derrière lui).
inline std::string uniqueTempNameForTick(const std::string& prefix,
                                         const std::string& ext,
                                         long long tick) {
    static std::atomic<unsigned long long> compteur{0};
    return prefix + std::to_string(::getpid()) + "_" +
           std::to_string(compteur.fetch_add(1)) + "_" +
           std::to_string(tick) + ext;
}

// Version de confort utilisée par les fixtures : tick = maintenant.
inline std::string uniqueTempName(const std::string& prefix,
                                  const std::string& ext) {
    return uniqueTempNameForTick(
        prefix, ext,
        static_cast<long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

} // namespace swingbot_test
