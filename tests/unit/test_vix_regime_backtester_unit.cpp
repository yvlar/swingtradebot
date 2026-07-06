// ============================================================
//  test_vix_regime_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VixRegimeBacktester (backtest/VixRegimeBacktester.hpp)
//  5e famille, variante à VOLATILITÉ IMPLICITE (Sprint 14, item 14.1). Deux séries
//  alignées : série 0 = actif TRADÉ (QQQ), série 1 = SIGNAL (VIX). Aucun I/O : on
//  exerce le seam fromAxis avec des scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce : avec refLookback = 2, la médiane de référence est la moyenne des deux
//  derniers VIX : med[i] = (vix[i-1]+vix[i])/2. Le régime CALME (vix[i] ≤ med[i])
//  se réduit à vix[i] ≤ vix[i-1] — « le VIX ne monte pas ». Warmup w = refLookback-1
//  = 1. À coûts nuls, les P&L (sur QQQ) tombent juste.
// ============================================================
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "backtest/VixRegimeBacktester.hpp"

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

// Config à COÛTS NULS, refLookback = 2, seuil = 1,0.
VixRegimeConfig cfg0() {
    VixRegimeConfig c;
    c.refLookback = 2; c.thresholdMult = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Arité : le moteur exige EXACTEMENT 2 séries (QQQ tradé + VIX signal).
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(5);
    ax.close = { {100,100,110,121,133}, {20,20,18,17,17}, {50,50,50,50,50} }; // 3 séries
    const auto r = VixRegimeBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  VIX plat → toujours calme (vix ≤ sa médiane) → LONG QQQ dès le warmup.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, FlatVixIsCalmGoesLong) {
    const auto ax = axisQqqVix(dates(5), {100,110,121,133.1,146.41},
                                         {20,20,20,20,20});
    const auto r = VixRegimeBacktester::fromAxis(cfg0(), ax).run();
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.switchCount, 1);
    EXPECT_NEAR(r.finalValue, 13'310.0, 1e-6);        // 10000·(146.41/110)
    EXPECT_NEAR(r.trades[0].pnlPct, (146.41 / 110.0 - 1.0) * 100.0, 1e-9);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
}

// ════════════════════════════════════════════════════════════
//  Le seuil GATE le régime : VIX plat mais thresholdMult=0,9 → VIX (20) > 0,9×20
//  (=18) → JAMAIS calme → CASH. Prouve que thresholdMult est bien exercé (D33).
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, ThresholdBelowOneKeepsFlatVixInCash) {
    VixRegimeConfig c = cfg0(); c.thresholdMult = 0.9;
    const auto ax = axisQqqVix(dates(5), {100,110,121,133.1,146.41},
                                         {20,20,20,20,20});
    const auto r = VixRegimeBacktester::fromAxis(c, ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.switchCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  VIX qui BAISSE → régime calme → LONG QQQ, P&L exact (sur QQQ, pas sur le VIX).
//  vix = [20,20,18,17,17] → calme dès i=1 ; qqq = [100,100,105,110,120].
//   entrée i=1, gains i1→i2→i3→i4 = 120/100 = +20 %, liquidation « fin » à i=4.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, CalmVixGoesLongWithExactPnl) {
    const auto ax = axisQqqVix(dates(5), {100,100,105,110,120},
                                         {20,20,18,17,17});
    const auto r = VixRegimeBacktester::fromAxis(cfg0(), ax).run();
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.switchCount, 1);
    EXPECT_NEAR(r.finalValue, 12'000.0, 1e-6);
    EXPECT_NEAR(r.totalReturnPct, 20.0, 1e-9);
    EXPECT_NEAR(r.trades[0].pnlPct, 20.0, 1e-9);       // gross = 120/100 (QQQ)
    EXPECT_EQ(r.trades[0].holdDays, 3);                // entrée i=1, sortie i=4
    EXPECT_EQ(r.trades[0].buyDate, dates(5)[1]);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_NEAR(r.pctTimeInvested, 75.0, 1e-9);        // 3 séances / M−1 = 4
}

// ════════════════════════════════════════════════════════════
//  VIX qui MONTE en continu → jamais calme → équité plate, aucun trade.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, HighVixStaysInCashFlatEquity) {
    const auto ax = axisQqqVix(dates(5), {100,110,120,130,140},
                                         {15,16,18,21,25});   // VIX strictement croissant
    const auto r = VixRegimeBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.switchCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Deux stints + garde anti-cash-drag (D47) : le VIX baisse (long), remonte (cash),
//  rebaisse (long). Entre les stints l'équité est PLATE (cash).
//  vix = [20,19,22,18,18,25,17] ; qqq = [100,100,110,100,100,120,100].
//   Stint 1 : i=1→i=2 (+10 %) ; cash i=2→i=3 (plat) ;
//   Stint 2 : i=3→i=5 (+20 %) ; cash ensuite.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, TwoStintsAndCashIsFlatBetween) {
    const auto ax = axisQqqVix(dates(7), {100,100,110,100,100,120,100},
                                         {20,19,22,18,18,25,17});
    const auto r = VixRegimeBacktester::fromAxis(cfg0(), ax).run();
    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_GT(r.trades.size(), 1u);                    // D47 : la famille TRADE
    EXPECT_EQ(r.switchCount, 4);                       // cash→long→cash→long→cash
    EXPECT_NEAR(r.trades[0].pnlPct, 10.0, 1e-9);
    EXPECT_NEAR(r.trades[1].pnlPct, 20.0, 1e-9);
    EXPECT_EQ(r.trades[0].exitReason, std::string("regime"));
    EXPECT_EQ(r.trades[1].exitReason, std::string("regime"));
    EXPECT_NEAR(r.totalReturnPct, 32.0, 1e-9);         // 1.10·1.20 − 1
    EXPECT_NEAR(r.finalValue, 13'200.0, 1e-6);
    // Équité plate entre les stints : mark au début de i=2 == mark au début de i=3.
    ASSERT_GE(r.equityCurve.size(), 4u);
    EXPECT_NEAR(r.equityCurve[2], r.equityCurve[3], 1e-9);
    EXPECT_NEAR(r.equityCurve[2], 11'000.0, 1e-6);
}

// ════════════════════════════════════════════════════════════
//  Sous-fenêtre plus courte que le warmup (refLookback) → aucun trade.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeBacktesterUnit, RangeShorterThanWarmupNeverTrades) {
    const auto ax = axisQqqVix(dates(7), {100,100,110,100,100,120,100},
                                         {20,19,22,18,18,25,17});
    VixRegimeConfig c = cfg0(); c.refLookback = 10;    // warmup 9 > longueur de la sous-fenêtre
    const auto r = VixRegimeBacktester::fromAxis(c, ax).runRange(0, 5);
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}
