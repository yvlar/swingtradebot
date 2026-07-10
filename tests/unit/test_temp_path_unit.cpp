// ============================================================
//  test_temp_path_unit.cpp  —  Tests UNITAIRES
//  Cible : tests/support/TempPath.hpp — unicité des noms de
//  fichiers temporaires des fixtures (D59).
//
//  Le scénario de collision réel (CI, run 29094492121) : deux
//  processus ctest démarrent dans le MÊME tick steady_clock et
//  dérivent le MÊME nom de base SQLite. Ici on le transpose de
//  façon déterministe : deux appels avec un tick FORCÉ identique
//  doivent quand même produire des noms distincts.
// ============================================================
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

#include "../support/TempPath.hpp"

using swingbot_test::uniqueTempName;
using swingbot_test::uniqueTempNameForTick;

// ── Le cœur de D59 : unicité à tick d'horloge IDENTIQUE ──────
// Rouge sur l'ancien schéma (nom dérivé du seul horodatage) :
// deux processus nés dans le même tick ouvraient le même fichier.
TEST(TempPathUnit, SameTickTwiceStillYieldsDistinctNames) {
    const std::string a = uniqueTempNameForTick("unit_db_", ".db", 42);
    const std::string b = uniqueTempNameForTick("unit_db_", ".db", 42);
    EXPECT_NE(a, b) << "Deux noms générés dans le même tick doivent différer "
                       "(collision inter-processus D59)";
}

// ── Unicité inter-processus : le PID fait partie du nom ──────
// C'est lui qui sépare deux processus ctest parallèles nés dans
// le même tick (le compteur atomique est par processus).
TEST(TempPathUnit, NameEmbedsProcessId) {
    const std::string name = uniqueTempName("unit_db_", ".db");
    EXPECT_NE(name.find(std::to_string(::getpid())), std::string::npos)
        << "Le nom doit contenir le PID pour être unique entre processus";
}

// ── Unicité intra-processus : appels successifs ──────────────
TEST(TempPathUnit, SuccessiveCallsDiffer) {
    const std::string a = uniqueTempName("unit_db_", ".db");
    const std::string b = uniqueTempName("unit_db_", ".db");
    EXPECT_NE(a, b);
}

// ── Contrat de forme : préfixe et extension préservés ────────
TEST(TempPathUnit, PrefixAndExtensionPreserved) {
    const std::string name = uniqueTempName("prefixe_", ".csv");
    ASSERT_GE(name.size(), std::string("prefixe_.csv").size());
    EXPECT_EQ(name.rfind("prefixe_", 0), 0u);
    EXPECT_EQ(name.substr(name.size() - 4), ".csv");
}
