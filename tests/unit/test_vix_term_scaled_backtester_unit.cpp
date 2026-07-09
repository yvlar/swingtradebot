// ============================================================
//  test_vix_term_scaled_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VixTermScaledBacktester (backtest/VixTermScaledBacktester.hpp)
//  Sprint 19 (piste §5.3), variante CONTINUE : le poids suit la forme de la
//  courbe de vol implicite — w* = min(maxWeight, (VIX3M/VIX)^steepness).
//  Contango → plafonné à 1 (investi plein) ; backwardation → poids ∝
//  profondeur de l'inversion. TROIS séries alignées : série 0 = actif TRADÉ
//  (QQQ), séries 1/2 = SIGNAUX (VIX, VIX3M, jamais tradés). Aucun I/O : seam
//  fromAxis, scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce : le ratio est instantané → AUCUN warmup, le poids est décidable
//  dès i = 0. Un couple (VIX 40, VIX3M 20) constant donne w = 0,5 constant
//  → P&L exact = capital · Π(1 + 0,5·r_qqq).
// ============================================================
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "backtest/VixTermScaledBacktester.hpp"

using namespace trading;

namespace {

// Axe aligné 3 séries : c0 = QQQ (tradé), c1 = VIX, c2 = VIX3M (signaux).
AlignedAxis axisQqqVixV3m(const std::vector<std::string>& dates,
                          const std::vector<double>& qqq,
                          const std::vector<double>& vix,
                          const std::vector<double>& v3m) {
    AlignedAxis ax;
    ax.dates = dates;
    ax.close = { qqq, vix, v3m };
    return ax;
}

std::vector<std::string> dates(size_t n) {
    std::vector<std::string> d;
    for (size_t i = 0; i < n; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "2020-01-%02d", static_cast<int>(i + 1));
        d.emplace_back(buf);
    }
    return d;
}

// Config à COÛTS NULS, pente 1, pas de levier.
VixTermScaledConfig cfg0() {
    VixTermScaledConfig c;
    c.steepness = 1.0; c.maxWeight = 1.0; c.rebalanceBand = 0.05;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Fonction pure : formule du poids, plafond, pente, gardes dégénérées.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, TargetWeightFormulaAndCap) {
    EXPECT_DOUBLE_EQ(tsTargetWeight(40.0, 20.0, 1.0, 1.0), 0.5);   // backwardation ×2
    EXPECT_DOUBLE_EQ(tsTargetWeight(15.0, 20.0, 1.0, 1.0), 1.0);   // contango → plafond
    EXPECT_DOUBLE_EQ(tsTargetWeight(40.0, 20.0, 2.0, 1.0), 0.25);  // pente 2 → carré
    EXPECT_DOUBLE_EQ(tsTargetWeight(0.0, 20.0, 1.0, 1.0), 0.0);    // VIX dégénéré
    EXPECT_DOUBLE_EQ(tsTargetWeight(40.0, -1.0, 1.0, 1.0), 0.0);   // VIX3M dégénéré
    EXPECT_DOUBLE_EQ(tsTargetWeight(15.0, 20.0, 1.0, 0.8), 0.8);   // plafond custom
}

// ════════════════════════════════════════════════════════════
//  Arité : le moteur exige EXACTEMENT 3 séries (tradé + VIX + VIX3M).
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(5);
    ax.close = { {100, 100, 110, 121, 133}, {20, 20, 18, 17, 17} };   // 2 séries
    const auto r = VixTermScaledBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  Contango constant (VIX 15 < VIX3M 20, ratio 4/3 plafonné à 1) → w = 1 dès
//  i = 0 : équité = B&H exact de QQQ (coûts nuls), poids moyen 1, zéro drag.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, ContangoFullWeightIsBuyAndHold) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {15, 15, 15, 15}, {20, 20, 20, 20});
    const auto r = VixTermScaledBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);                    // l'entrée cash→1 seulement
    EXPECT_NEAR(r.finalValue, 10'000.0 * qqq.back() / qqq.front(), 1e-9);
    EXPECT_NEAR(r.avgWeight, 1.0, 1e-12);
    EXPECT_NEAR(r.alphaVsBuyHold, 0.0, 1e-9);          // investi plein = B&H
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
}

// ════════════════════════════════════════════════════════════
//  Backwardation constante ×2 (VIX 40, VIX3M 20) → w = 0,5 constant :
//  P&L exact = Π(1 + 0,5·r_qqq) à 1e-9, turnover 1,0 (entrée + liquidation).
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, BackwardationHalfWeightExactPnl) {
    const std::vector<double> qqq = {100, 110, 121, 133.1, 146.41};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {40, 40, 40, 40, 40}, {20, 20, 20, 20, 20});
    const auto r = VixTermScaledBacktester::fromAxis(cfg0(), ax).run();

    double expected = 10'000.0;
    for (size_t i = 0; i + 1 < qqq.size(); ++i)
        expected *= (1.0 + 0.5 * (qqq[i + 1] / qqq[i] - 1.0));

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_NEAR(r.avgWeight, 0.5, 1e-12);
    EXPECT_NEAR(r.pctTimeInvested, 100.0, 1e-9);
    EXPECT_NEAR(r.turnover, 1.0, 1e-12);               // 0,5 entrée + 0,5 liquidation
}

// ════════════════════════════════════════════════════════════
//  Pente : steepness 2 met le ratio au carré — inversion ×2 → w = 0,25 exact.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, SteepnessSquaresTheRatio) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    VixTermScaledConfig c = cfg0();
    c.steepness = 2.0;
    const auto r = VixTermScaledBacktester::fromAxis(
        c, axisQqqVixV3m(dates(qqq.size()), qqq,
                         {40, 40, 40, 40}, {20, 20, 20, 20})).run();

    double expected = 10'000.0;
    for (size_t i = 0; i + 1 < qqq.size(); ++i)
        expected *= (1.0 + 0.25 * (qqq[i + 1] / qqq[i] - 1.0));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_NEAR(r.avgWeight, 0.25, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  Bande anti-churn : VIX3M 20, VIX 40 → 38 fait passer w* de 0,5 à ~0,526
//  (|Δ| ≈ 0,026 ≤ 0,05) → AUCUN rebalancement, le poids d'entrée est conservé.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, RatioDriftWithinBandKeepsWeight) {
    const std::vector<double> qqq = {100, 102, 104, 106, 108};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {40, 38, 39, 38, 40}, {20, 20, 20, 20, 20});
    const auto r = VixTermScaledBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);                    // l'entrée seulement
    double expected = 10'000.0;
    for (size_t i = 0; i + 1 < qqq.size(); ++i)
        expected *= (1.0 + 0.5 * (qqq[i + 1] / qqq[i] - 1.0));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Inversion brutale : contango (w=1) → backwardation ×2 (w=0,5) rebalance
//  avec coût ∝ |Δw| ; à coûts non nuls l'équité est exacte, 2 stints.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, InversionSpikeRebalancesWithProportionalCost) {
    VixTermScaledConfig c = cfg0();
    c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
    const double perSide = 0.001 + (2.0 + 0.5) / 10'000.0;

    const std::vector<double> qqq = {100, 102, 104, 106};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {15, 40, 40, 40}, {20, 20, 20, 20});
    const auto r = VixTermScaledBacktester::fromAxis(c, ax).run();

    // i=0 : contango, entrée à w=1 (coût 1·c) ; gagne 1·r0 ;
    // i=1 : inversion ×2 → w*=0,5, rebal (coût 0,5·c) ; gagne 0,5·r1 puis 0,5·r2 ;
    // fin : liquidation (coût 0,5·c).
    double expected = 10'000.0 * (1.0 - 1.0 * perSide);
    expected *= (1.0 + 1.0 * (qqq[1] / qqq[0] - 1.0));
    expected *= (1.0 - 0.5 * perSide);
    expected *= (1.0 + 0.5 * (qqq[2] / qqq[1] - 1.0));
    expected *= (1.0 + 0.5 * (qqq[3] / qqq[2] - 1.0));
    expected *= (1.0 - 0.5 * perSide);

    ASSERT_EQ(r.trades.size(), 2u);                    // stint w=1 puis stint w=0,5
    EXPECT_EQ(r.rebalanceCount, 2);
    EXPECT_EQ(r.trades[0].exitReason, std::string("rebal"));
    EXPECT_EQ(r.trades[1].exitReason, std::string("fin"));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_NEAR(r.turnover, 1.0 + 0.5 + 0.5, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  Signal dégénéré (VIX3M ≤ 0) → poids 0 (cash) : pas de division par zéro,
//  pas de position sur signal cassé.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, DegenerateSignalYieldsZeroWeight) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    const auto r = VixTermScaledBacktester::fromAxis(
        cfg0(), axisQqqVixV3m(dates(qqq.size()), qqq,
                              {15, 15, 15, 15}, {20, 0, 20, 20})).run();
    // i=0 : w=1 ; i=1 : signal cassé → w*=0 → sortie ; i=2 : ré-entrée w=1 ;
    // fin : liquidation. Coûts nuls → P&L = r0 puis r2.
    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_EQ(r.trades[0].exitReason, std::string("cash"));
    EXPECT_NEAR(r.finalValue,
                10'000.0 * (qqq[1] / qqq[0]) * (qqq[3] / qqq[2]), 1e-9);
}

// ════════════════════════════════════════════════════════════
//  SEULE la série 0 (QQQ) produit du P&L : QQQ plat + signaux mobiles →
//  équité PLATE (les indices de vol ne sont jamais achetés).
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledBacktesterUnit, OnlyTradedSeriesAccruesPnl) {
    const std::vector<double> qqq = {100, 100, 100, 100, 100};
    VixTermScaledConfig c = cfg0();
    c.rebalanceBand = 0.0;   // rebalance à chaque variation de poids (coûts nuls)
    const auto r = VixTermScaledBacktester::fromAxis(
        c, axisQqqVixV3m(dates(qqq.size()), qqq,
                         {40, 20, 50, 25, 40}, {20, 20, 20, 20, 20})).run();
    EXPECT_NEAR(r.finalValue, 10'000.0, 1e-9);         // QQQ plat → aucun P&L
    EXPECT_GE(r.rebalanceCount, 3);                    // le poids, lui, a bougé
}
