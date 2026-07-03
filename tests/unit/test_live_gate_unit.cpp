// ============================================================
//  test_live_gate_unit.cpp  —  Tests UNITAIRES
//  Cible : LiveGate — gate mécanique du mode --live (A1)
//  BUG : --live n'était qu'un flag + 5 s de sleep ; un conteneur
//  redémarré automatiquement repartait en argent réel sans
//  opérateur.
// ============================================================
#include <gtest/gtest.h>
#include <sstream>
#include "core/LiveGate.hpp"

using namespace trading;

namespace {

// Entrée « tout au vert » : chaque test dégrade UNE couche
LiveGateInput allGreen() {
    LiveGateInput in;
    in.liveFlag          = true;
    in.approvedInConfig  = true;
    in.stdinIsTty        = true;
    in.alertChannelReady = true;
    return in;
}

} // namespace

// Mode paper : aucun gate, aucune question posée
TEST(LiveGateUnit, PaperModeIsAlwaysAllowedWithoutQuestion) {
    LiveGateInput in;                   // liveFlag = false
    std::istringstream vide("");
    EXPECT_FALSE(liveGateRefusal(in, vide).has_value());
}

// Couche 1 : config non approuvée → refus nommant le RUNBOOK
TEST(LiveGateUnit, RefusedWhenConfigNotApproved) {
    auto in = allGreen();
    in.approvedInConfig = false;
    std::istringstream oui("OUI\n");
    auto refus = liveGateRefusal(in, oui);
    ASSERT_TRUE(refus.has_value());
    EXPECT_NE(refus->find("liveTradingApproved"), std::string::npos);
}

// Couche 2 : aucun canal d'alerte → refus
TEST(LiveGateUnit, RefusedWithoutAlertChannel) {
    auto in = allGreen();
    in.alertChannelReady = false;
    std::istringstream oui("OUI\n");
    auto refus = liveGateRefusal(in, oui);
    ASSERT_TRUE(refus.has_value());
    EXPECT_NE(refus->find("alerte"), std::string::npos);
}

// Couche 3 : pas de TTY (conteneur détaché, restart auto) → refus
TEST(LiveGateUnit, RefusedWithoutTty) {
    auto in = allGreen();
    in.stdinIsTty = false;
    std::istringstream oui("OUI\n");
    auto refus = liveGateRefusal(in, oui);
    ASSERT_TRUE(refus.has_value());
    EXPECT_NE(refus->find("terminal"), std::string::npos);
}

// Couche 4 : confirmation ≠ « OUI » exact (y compris « oui ») → refus
TEST(LiveGateUnit, RefusedOnWrongConfirmation) {
    auto in = allGreen();
    std::istringstream non("non\n");
    EXPECT_TRUE(liveGateRefusal(in, non).has_value());
    std::istringstream minuscule("oui\n");
    EXPECT_TRUE(liveGateRefusal(in, minuscule).has_value());
}

// Couche 4 bis : EOF (stdin fermé) → refus
TEST(LiveGateUnit, RefusedOnEof) {
    auto in = allGreen();
    std::istringstream vide("");
    auto refus = liveGateRefusal(in, vide);
    ASSERT_TRUE(refus.has_value());
    EXPECT_NE(refus->find("EOF"), std::string::npos);
}

// Toutes les couches au vert + « OUI » tapé → autorisé
TEST(LiveGateUnit, AllowedWhenEverythingGreenAndOuiTyped) {
    auto in = allGreen();
    std::istringstream oui("OUI\n");
    EXPECT_FALSE(liveGateRefusal(in, oui).has_value());
}
