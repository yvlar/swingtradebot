// ============================================================
//  test_vix_scaled_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VixScaledBacktester (backtest/VixScaledBacktester.hpp)
//  Réouverture Sprint 18 (piste §5.2), variante à vol IMPLICITE : scaling
//  continu w* = min(maxWeight, cible / VIX). Deux séries alignées : série 0 =
//  actif TRADÉ (QQQ), série 1 = SIGNAL (VIX, jamais tradé). Aucun I/O : seam
//  fromAxis, scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce : le VIX est DÉJÀ une vol annualisée en % → aucun warmup, le poids
//  est décidable dès i = 0. Un VIX constant à 30 avec cible 15 donne w = 0,5
//  constant → P&L exact = capital · Π(1 + 0,5·r_qqq).
// ============================================================
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "backtest/VixScaledBacktester.hpp"

using namespace trading;

namespace {

// Axe aligné 2 séries : c0 = QQQ (tradé), c1 = VIX (signal).
AlignedAxis axisQqqVix(const std::vector<std::string>& dates,
                       const std::vector<double>& qqq,
                       const std::vector<double>& vix) {
    AlignedAxis ax;
    ax.dates = dates;
    ax.close = { qqq, vix };
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

// Config à COÛTS NULS, cible 15 % (défaut), pas de levier.
VixScaledConfig cfg0() {
    VixScaledConfig c;
    c.targetVixPct = 15.0; c.maxWeight = 1.0; c.rebalanceBand = 0.05;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Arité : le moteur exige EXACTEMENT 2 séries (QQQ tradé + VIX signal).
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(5);
    ax.close = { {100,100,110,121,133}, {20,20,18,17,17}, {50,50,50,50,50} }; // 3 séries
    const auto r = VixScaledBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  Poids depuis le niveau du VIX : VIX 30, cible 15 → w = 0,5 dès i = 0
//  (AUCUN warmup), P&L exact = Π(1 + 0,5·r_qqq) de la barre 0 à la fin.
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, ConstantVixHalfWeightExactPnl) {
    const std::vector<double> qqq = {100, 110, 121, 133.1, 146.41};
    const auto ax = axisQqqVix(dates(qqq.size()), qqq, {30, 30, 30, 30, 30});
    const auto r = VixScaledBacktester::fromAxis(cfg0(), ax).run();

    double expected = 10'000.0;
    for (size_t i = 0; i + 1 < qqq.size(); ++i)
        expected *= (1.0 + 0.5 * (qqq[i + 1] / qqq[i] - 1.0));

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);                    // entrée cash→0,5 à i=0
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_EQ(r.trades[0].buyDate, dates(qqq.size())[0]);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
    // Poids moyen = 0,5 sur les 4 séances ; 100 % du temps investi.
    EXPECT_NEAR(r.avgWeight, 0.5, 1e-12);
    EXPECT_NEAR(r.pctTimeInvested, 100.0, 1e-9);
    EXPECT_NEAR(r.turnover, 1.0, 1e-12);               // 0,5 entrée + 0,5 liquidation
}

// ════════════════════════════════════════════════════════════
//  VIX très bas → w plafonné à maxWeight (pas de levier) : VIX 10, cible 15
//  → 1,5 plafonné à 1,0 → équité = B&H exact de QQQ (coûts nuls).
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, LowVixIsCappedAtMaxWeight) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    const auto ax = axisQqqVix(dates(qqq.size()), qqq, {10, 10, 10, 10});
    const auto r = VixScaledBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_NEAR(r.finalValue, 10'000.0 * qqq.back() / qqq.front(), 1e-9);
    EXPECT_NEAR(r.avgWeight, 1.0, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  SEULE la série 0 (QQQ) produit du P&L : QQQ plat + VIX qui bouge → équité
//  PLATE (le VIX n'est jamais acheté ; ses variations ne valent que par le poids).
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, OnlyTradedSeriesAccruesPnl) {
    const std::vector<double> qqq = {100, 100, 100, 100, 100};
    const auto ax = axisQqqVix(dates(qqq.size()), qqq, {30, 15, 60, 10, 25});
    VixScaledConfig c = cfg0();
    c.rebalanceBand = 0.0;   // rebalance à chaque variation de poids (coûts nuls)
    const auto r = VixScaledBacktester::fromAxis(c, ax).run();

    EXPECT_NEAR(r.finalValue, 10'000.0, 1e-9);         // QQQ plat → aucun P&L
    EXPECT_GE(r.rebalanceCount, 3);                    // le poids, lui, a bougé
}

// ════════════════════════════════════════════════════════════
//  Bande anti-churn : VIX 30 → 28 fait passer w* de 0,5 à ~0,536 (|Δ| ≈ 0,036
//  ≤ 0,05) → AUCUN rebalancement, le poids d'entrée 0,5 est conservé.
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, VixDriftWithinBandKeepsWeight) {
    const std::vector<double> qqq = {100, 102, 104, 106, 108};
    const auto ax = axisQqqVix(dates(qqq.size()), qqq, {30, 28, 29, 28, 30});
    const auto r = VixScaledBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);                    // l'entrée seulement
    double expected = 10'000.0;
    for (size_t i = 0; i + 1 < qqq.size(); ++i)
        expected *= (1.0 + 0.5 * (qqq[i + 1] / qqq[i] - 1.0));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Choc de VIX : 30 → 60 divise le poids par 2 (0,5 → 0,25, |Δ| = 0,25 > bande)
//  → rebalancement facturé ∝ |Δw| ; à coûts non nuls l'équité est exacte.
// ════════════════════════════════════════════════════════════
TEST(VixScaledBacktesterUnit, VixSpikeHalvesWeightWithProportionalCost) {
    VixScaledConfig c = cfg0();
    c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
    const double perSide = 0.001 + (2.0 + 0.5) / 10'000.0;

    const std::vector<double> qqq = {100, 102, 104, 106};
    const auto ax = axisQqqVix(dates(qqq.size()), qqq, {30, 60, 60, 60});
    const auto r = VixScaledBacktester::fromAxis(c, ax).run();

    // i=0 : entrée à 0,5 (coût 0,5·c) ; gagne 0,5·r0 ;
    // i=1 : VIX 60 → w*=0,25, rebal (coût 0,25·c) ; gagne 0,25·r1 puis 0,25·r2 ;
    // fin : liquidation (coût 0,25·c).
    double expected = 10'000.0 * (1.0 - 0.5 * perSide);
    expected *= (1.0 + 0.5 * (qqq[1] / qqq[0] - 1.0));
    expected *= (1.0 - 0.25 * perSide);
    expected *= (1.0 + 0.25 * (qqq[2] / qqq[1] - 1.0));
    expected *= (1.0 + 0.25 * (qqq[3] / qqq[2] - 1.0));
    expected *= (1.0 - 0.25 * perSide);

    ASSERT_EQ(r.trades.size(), 2u);                    // stint 0,5 puis stint 0,25
    EXPECT_EQ(r.rebalanceCount, 2);
    EXPECT_EQ(r.trades[0].exitReason, std::string("rebal"));
    EXPECT_EQ(r.trades[1].exitReason, std::string("fin"));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_NEAR(r.turnover, 0.5 + 0.25 + 0.25, 1e-12);
}
