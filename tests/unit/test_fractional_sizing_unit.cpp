// ============================================================
//  test_fractional_sizing_unit.cpp  —  Tests UNITAIRES
//  Cible : analyzeFractionalSizing (Sprint 23, item 23.5)
//  Contre-factuel fractionnaire STRICTEMENT offline : mêmes
//  signaux, même formule de risque, quantités théoriques.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/FractionalSizingAnalysis.hpp"

using namespace trading;

namespace {

EntryDecision decision(double cash, double price, int shares,
                       EntryOutcome outcome = EntryOutcome::Executee) {
    EntryDecision d;
    d.date = "2024-01-02"; d.cash = cash; d.price = price;
    d.shares = shares; d.outcome = outcome;
    return d;
}

} // namespace

// L'EXEMPLE CANONIQUE du sprint : 1 000 $, risque 1 %, stop 5 %, prix élevé
// (500 $). Fractionnaire = 1 000 × 0,01 / (500 × 0,05) = 0,4 action ;
// entière = 0 → signal bloqué UNIQUEMENT par la contrainte entière.
// Déploiement fractionnaire = 0,4 × 500 / 1 000 = 20 % ; entier = 0 % ;
// perte de déploiement = 20 points.
TEST(FractionalSizingUnit, CanonicalSmallAccountExampleAtHighPrice) {
    const std::vector<EntryDecision> ds = {
        decision(1'000.0, 500.0, 0, EntryOutcome::RejetQuantiteZero),
    };
    const auto a = analyzeFractionalSizing(ds, /*risk=*/0.01, /*stop=*/0.05);

    EXPECT_EQ(a.signalsAnalyzed, 1);
    EXPECT_EQ(a.signalsBlockedOnlyByIntegerConstraint, 1);
    EXPECT_NEAR(a.avgTheoreticalFractionalQuantity, 0.4, 1e-9);
    EXPECT_NEAR(a.avgIntegerQuantity,               0.0, 1e-9);
    EXPECT_NEAR(a.fractionalDeploymentPct,         20.0, 1e-9);
    EXPECT_NEAR(a.integerDeploymentPct,             0.0, 1e-9);
    EXPECT_NEAR(a.lostDeploymentPctDueToIntegerConstraint, 20.0, 1e-9);
}

// Quantité fractionnaire plafonnée par le CAPITAL utilisable (95 %), pas par
// le risque : cash 1 000 $, prix 100 $, risque 60 % → par risque = 120
// actions mais par capital = 9,5 → le contre-factuel retient 9,5.
TEST(FractionalSizingUnit, FractionalQuantityIsCappedByUsableCapital) {
    const std::vector<EntryDecision> ds = { decision(1'000.0, 100.0, 9) };
    const auto a = analyzeFractionalSizing(ds, /*risk=*/0.60, /*stop=*/0.05);

    EXPECT_NEAR(a.avgTheoreticalFractionalQuantity, 9.5, 1e-9);
    EXPECT_NEAR(a.fractionalDeploymentPct, 95.0, 1e-9);
    EXPECT_NEAR(a.integerDeploymentPct,    90.0, 1e-9);
    EXPECT_NEAR(a.lostDeploymentPctDueToIntegerConstraint, 5.0, 1e-9);
    EXPECT_EQ(a.signalsBlockedOnlyByIntegerConstraint, 0);
}

// Moyennes sur plusieurs signaux (risque 2 %, stop 5 %) : un exécuté
// (1 action entière sur 1,6 théorique — 1 000 × 0,02 / (250 × 0,05)) et un
// bloqué (0 sur 0,8 — 1 000 × 0,02 / (500 × 0,05)) → moyennes à la main.
TEST(FractionalSizingUnit, AveragesAccumulateAcrossDecisions) {
    const std::vector<EntryDecision> ds = {
        decision(1'000.0, 250.0, 1),                                  // frac 1,6
        decision(1'000.0, 500.0, 0, EntryOutcome::RejetQuantiteZero), // frac 0,8
    };
    const auto a = analyzeFractionalSizing(ds, 0.02, 0.05);

    EXPECT_EQ(a.signalsAnalyzed, 2);
    EXPECT_EQ(a.signalsBlockedOnlyByIntegerConstraint, 1);
    EXPECT_NEAR(a.avgTheoreticalFractionalQuantity, (1.6 + 0.8) / 2.0, 1e-9);
    EXPECT_NEAR(a.avgIntegerQuantity, 0.5, 1e-9);
    // Déploiements : exécuté 25 % entier vs 40 % théorique ; bloqué 0 % vs 40 %.
    EXPECT_NEAR(a.integerDeploymentPct,    (25.0 + 0.0) / 2.0, 1e-9);
    EXPECT_NEAR(a.fractionalDeploymentPct, (40.0 + 40.0) / 2.0, 1e-9);
    EXPECT_NEAR(a.lostDeploymentPctDueToIntegerConstraint, 27.5, 1e-9);
}

// Un GRAND compte n'est quasiment pas contraint : 100 000 $, prix 500 $,
// risque 1 % → 40 actions entières sur 40 théoriques — la perte de
// déploiement tend vers zéro (la contrainte entière est un problème de
// PETIT compte).
TEST(FractionalSizingUnit, LargeAccountLosesAlmostNothingToIntegerConstraint) {
    const std::vector<EntryDecision> ds = { decision(100'000.0, 500.0, 40) };
    const auto a = analyzeFractionalSizing(ds, 0.01, 0.05);

    EXPECT_EQ(a.signalsBlockedOnlyByIntegerConstraint, 0);
    EXPECT_NEAR(a.lostDeploymentPctDueToIntegerConstraint, 0.0, 1e-9);
}

// Entrées dégénérées : vide ou paramètres invalides → résultat neutre
TEST(FractionalSizingUnit, DegenerateInputsGiveNeutralResult) {
    EXPECT_EQ(analyzeFractionalSizing({}, 0.01, 0.05).signalsAnalyzed, 0);
    const std::vector<EntryDecision> ds = { decision(1'000.0, 500.0, 0) };
    EXPECT_EQ(analyzeFractionalSizing(ds, 0.01, 0.0).signalsAnalyzed, 0);
    EXPECT_EQ(analyzeFractionalSizing(ds, 0.0, 0.05).signalsAnalyzed, 0);
}
