// ============================================================
//  test_small_account_stress_unit.cpp  —  Tests UNITAIRES
//  Cible : stress de survie du petit compte (Sprint 23, 23.6)
//  Séquences de pertes en composition réelle, gaps au-delà du
//  stop, coûts inclus — attendus calculés à la main.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/SmallAccountStress.hpp"

using namespace trading;

namespace {
// Coûts nuls pour les attendus « à la main » ; les scénarios chiffrés du
// harnais utilisent les vrais modèles (verrous d'intégration).
const ExecutionCostConfig kSansCouts{};
} // namespace

// ════════════════════════════════════════════════════════════
//  Séquences de pertes — composition réelle
// ════════════════════════════════════════════════════════════

// 1 000 $, risque 2 %, stop 5 %, prix 100 $, sans coûts :
//   trade 1 : 4 actions (budget 20 $ / risque par action 5 $), perte 20 $
//   trade 2 : cash 980 → 3 actions (19,6/5 = 3,92 → 3), perte 15 $
//   trade 3 : cash 965 → 3 actions, perte 15 $
//   trade 4 : cash 950 → 3 actions, perte 15 $
//   trade 5 : cash 935 → 3 actions, perte 15 $ → 920 $
// La COMPOSITION (re-sizing entier sur capital restant) donne −80 $,
// PAS 5 × 20 $ = −100 $ : c'est exactement ce que le test verrouille.
TEST(SmallAccountStressUnit, FiveLossSequenceCompoundsWithIntegerSizing) {
    const auto r = simulateLossSequence(1'000.0, 0.02, 0.05, 5, 100.0,
                                        kSansCouts);
    EXPECT_EQ(r.tradesExecuted, 5);
    EXPECT_EQ(r.tradesBlocked,  0);
    EXPECT_NEAR(r.capitalRemaining, 920.0, 1e-9);
    EXPECT_NEAR(r.capitalLossPct,     8.0, 1e-9);
    EXPECT_NEAR(r.drawdownPct,        8.0, 1e-9);
    // Différent du naïf « n × risque initial » (−10 %)
    EXPECT_LT(r.capitalLossPct, 10.0);
}

// La séquence de 10 pertes prolonge la composition : chaque perte est
// bornée par le budget de risque du capital RESTANT → la perte totale
// reste STRICTEMENT sous 10 × 2 % = 20 %.
TEST(SmallAccountStressUnit, TenLossSequenceStaysUnderNaiveLinearLoss) {
    const auto r = simulateLossSequence(1'000.0, 0.02, 0.05, 10, 100.0,
                                        kSansCouts);
    EXPECT_EQ(r.tradesExecuted + r.tradesBlocked, 10);
    EXPECT_GT(r.capitalLossPct, 8.0);    // pire que 5 pertes…
    EXPECT_LT(r.capitalLossPct, 20.0);   // …mais sous le naïf linéaire
}

// Prix élevé : le sizing entier tombe à zéro → trades BLOQUÉS, capital
// intact. La survie du petit compte est « protégée » par son incapacité
// à trader — le rapport doit le montrer, pas le cacher.
TEST(SmallAccountStressUnit, UnaffordableSharesBlockTheSequence) {
    // risque 0,5 % : budget 5 $ / risque par action 25 $ → 0 action
    const auto r = simulateLossSequence(1'000.0, 0.005, 0.05, 5, 500.0,
                                        kSansCouts);
    EXPECT_EQ(r.tradesExecuted, 0);
    EXPECT_EQ(r.tradesBlocked,  5);
    EXPECT_NEAR(r.capitalRemaining, 1'000.0, 1e-9);
    EXPECT_NEAR(r.capitalLossPct, 0.0, 1e-9);
}

// Les coûts aggravent la séquence : mêmes 5 pertes avec un minimum de
// commission de 1 $ par ordre → 2 $ de plus par aller-retour.
TEST(SmallAccountStressUnit, CostsWorsenTheLossSequence) {
    ExecutionCostConfig avecMin;
    avecMin.minimumCommissionPerOrder = 1.0;
    const auto sans = simulateLossSequence(1'000.0, 0.02, 0.05, 5, 100.0,
                                           kSansCouts);
    const auto avec = simulateLossSequence(1'000.0, 0.02, 0.05, 5, 100.0,
                                           avecMin);
    EXPECT_LT(avec.capitalRemaining, sans.capitalRemaining);
    // 5 allers-retours × 2 $ de minimum = 10 $ de frais en plus (les
    // quantités re-dimensionnées peuvent varier d'une action → tolérance)
    EXPECT_NEAR(sans.capitalRemaining - avec.capitalRemaining, 10.0, 5.0);
}

// ════════════════════════════════════════════════════════════
//  Gaps défavorables — sortie au-delà du stop
// ════════════════════════════════════════════════════════════

// Stop prévu −5 % mais fill à −10 % : la perte réelle DOUBLE la perte
// planifiée. 1 000 $, risque 2 %, prix 100 $ : 4 actions, perte 40 $
// au lieu des 20 $ planifiés.
TEST(SmallAccountStressUnit, GapBeyondStopExceedsPlannedRisk) {
    const auto r = simulateGapExit(1'000.0, 0.02, 0.05, 0.10, 100.0,
                                   kSansCouts);
    ASSERT_TRUE(r.executed);
    EXPECT_NEAR(r.plannedLossDollars, 20.0, 1e-9);
    EXPECT_NEAR(r.lossDollars,        40.0, 1e-9);
    EXPECT_NEAR(r.lossPctOfCapital,    4.0, 1e-9);
    EXPECT_GT(r.lossDollars, r.plannedLossDollars);
}

// Fill exactement au stop (−5 %) sans coûts : la perte colle au plan —
// contrôle de cohérence du simulateur (4 actions × 5 $ = 20 $).
TEST(SmallAccountStressUnit, FillAtStopMatchesPlannedRiskWithoutCosts) {
    const auto r = simulateGapExit(1'000.0, 0.02, 0.05, 0.05, 100.0,
                                   kSansCouts);
    ASSERT_TRUE(r.executed);
    EXPECT_NEAR(r.lossDollars, r.plannedLossDollars, 1e-9);
}

// Les coûts s'ajoutent au gap : mêmes 4 actions à −6 %, minimum 1 $ par
// ordre → perte = 4 × 6 $ + 2 $ = 26 $ (> 24 $ hors coûts).
TEST(SmallAccountStressUnit, GapLossIncludesCosts) {
    ExecutionCostConfig avecMin;
    avecMin.minimumCommissionPerOrder = 1.0;
    const auto r = simulateGapExit(1'000.0, 0.02, 0.05, 0.06, 100.0, avecMin);
    ASSERT_TRUE(r.executed);
    EXPECT_NEAR(r.lossDollars, 4 * 6.0 + 2.0, 1e-9);
}

// Le slippage dégrade l'entrée ET la sortie : à 25 bps par côté la perte
// d'un aller-retour au stop dépasse strictement celle à 2 bps.
TEST(SmallAccountStressUnit, HigherSlippageWorsensStopLoss) {
    ExecutionCostConfig bas;  bas.slippageBps  = 2.0;
    ExecutionCostConfig haut; haut.slippageBps = 25.0;
    const auto rBas  = simulateGapExit(1'000.0, 0.02, 0.05, 0.05, 100.0, bas);
    const auto rHaut = simulateGapExit(1'000.0, 0.02, 0.05, 0.05, 100.0, haut);
    ASSERT_TRUE(rBas.executed);
    ASSERT_TRUE(rHaut.executed);
    EXPECT_GT(rHaut.lossDollars, rBas.lossDollars);
}

// Déterminisme : deux exécutions identiques → mêmes chiffres exactement
TEST(SmallAccountStressUnit, StressScenariosAreDeterministic) {
    const auto a = simulateLossSequence(1'000.0, 0.01, 0.05, 10, 173.0,
                                        ExecutionCostConfig::stressPetitCompte());
    const auto b = simulateLossSequence(1'000.0, 0.01, 0.05, 10, 173.0,
                                        ExecutionCostConfig::stressPetitCompte());
    EXPECT_DOUBLE_EQ(a.capitalRemaining, b.capitalRemaining);
    EXPECT_EQ(a.tradesExecuted, b.tradesExecuted);
}
