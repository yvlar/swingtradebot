// ============================================================
//  test_vix_term_regime_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VixTermRegimeBacktester (backtest/VixTermRegimeBacktester.hpp)
//  Sprint 19 (piste §5.3), term-structure VIX/VIX3M : filtre BINAIRE
//  long/cash sur le ratio VIX/VIX3M lissé — contango (ratio ≤ seuil) → LONG,
//  backwardation ou inconnu (warmup, donnée dégénérée) → CASH. TROIS séries
//  alignées : série 0 = actif TRADÉ (QQQ), séries 1/2 = SIGNAUX (VIX, VIX3M,
//  jamais tradés). Aucun I/O : seam fromAxis, scénarios DÉTERMINISTES
//  calculés à la main.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/VixTermRegimeBacktester.hpp"

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

// Config à COÛTS NULS, seuil 1,0, SANS lissage (ratio brut) : les scénarios
// à la main restent lisibles ; le lissage a ses tests dédiés.
VixTermRegimeConfig cfg0() {
    VixTermRegimeConfig c;
    c.ratioThreshold = 1.0; c.smoothLookback = 1;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Fonctions pures : ratio (0 = non valide) et SMA trailing (fenêtre pleine
//  et valide exigées — « inconnu ≠ calme »).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, PureRatioAndSmaAreExact) {
    const auto ratio = tsRatio({20, 30, 0, 15}, {20, 20, 20, -1});
    ASSERT_EQ(ratio.size(), 4u);
    EXPECT_DOUBLE_EQ(ratio[0], 1.0);
    EXPECT_DOUBLE_EQ(ratio[1], 1.5);
    EXPECT_DOUBLE_EQ(ratio[2], 0.0);   // VIX dégénéré → non valide
    EXPECT_DOUBLE_EQ(ratio[3], 0.0);   // VIX3M dégénéré → non valide

    const auto s = tsSmaTrailing({1.0, 2.0, 3.0, 0.0, 5.0}, 2);
    ASSERT_EQ(s.size(), 5u);
    EXPECT_DOUBLE_EQ(s[0], 0.0);       // warmup (fenêtre incomplète)
    EXPECT_DOUBLE_EQ(s[1], 1.5);
    EXPECT_DOUBLE_EQ(s[2], 2.5);
    EXPECT_DOUBLE_EQ(s[3], 0.0);       // fenêtre contenant une barre non valide
    EXPECT_DOUBLE_EQ(s[4], 0.0);       // idem (le 0.0 est dans [3;4])
}

// ════════════════════════════════════════════════════════════
//  Arité : le moteur exige EXACTEMENT 3 séries (tradé + VIX + VIX3M).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(5);
    ax.close = { {100, 100, 110, 121, 133}, {20, 20, 18, 17, 17} };   // 2 séries
    const auto r = VixTermRegimeBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  Contango constant (VIX 15 < VIX3M 20, ratio 0,75 ≤ 1) → LONG dès i = 0
//  (ratio brut, pas de warmup) : équité = B&H exact de QQQ (coûts nuls).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, ContangoGoesLongWithExactPnl) {
    const std::vector<double> qqq = {100, 110, 121, 133.1, 146.41};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {15, 15, 15, 15, 15}, {20, 20, 20, 20, 20});
    const auto r = VixTermRegimeBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.switchCount, 1);                        // l'entrée seulement
    EXPECT_NEAR(r.finalValue, 10'000.0 * qqq.back() / qqq.front(), 1e-9);
    EXPECT_EQ(r.trades[0].buyDate, dates(qqq.size())[0]);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
    EXPECT_NEAR(r.pctTimeInvested, 100.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Backwardation constante (VIX 30 > VIX3M 20, ratio 1,5) → CASH permanent :
//  équité PLATE, aucun trade, 0 % du temps investi.
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, BackwardationStaysInCashFlatEquity) {
    const std::vector<double> qqq = {100, 90, 80, 95, 105};
    const auto ax = axisQqqVixV3m(dates(qqq.size()), qqq,
                                  {30, 30, 30, 30, 30}, {20, 20, 20, 20, 20});
    const auto r = VixTermRegimeBacktester::fromAxis(cfg0(), ax).run();

    EXPECT_TRUE(r.trades.empty());
    EXPECT_NEAR(r.finalValue, 10'000.0, 1e-9);
    EXPECT_EQ(r.switchCount, 0);
    EXPECT_NEAR(r.pctTimeInvested, 0.0, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  Frontière : ratio == seuil → LONG (convention ≤, la courbe PLATE est un
//  contango limite) ; un seuil plus strict (0,95) renvoie le même ratio en cash.
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, ThresholdBoundaryIsLong) {
    const std::vector<double> qqq = {100, 110, 121};
    const std::vector<double> vix = {20, 20, 20}, v3m = {20, 20, 20};   // ratio 1,0

    const auto rLong = VixTermRegimeBacktester::fromAxis(
        cfg0(), axisQqqVixV3m(dates(qqq.size()), qqq, vix, v3m)).run();
    ASSERT_EQ(rLong.trades.size(), 1u);                 // 1,0 ≤ 1,0 → LONG

    VixTermRegimeConfig strict = cfg0();
    strict.ratioThreshold = 0.95;
    const auto rCash = VixTermRegimeBacktester::fromAxis(
        strict, axisQqqVixV3m(dates(qqq.size()), qqq, vix, v3m)).run();
    EXPECT_TRUE(rCash.trades.empty());                  // 1,0 > 0,95 → CASH
    EXPECT_NEAR(rCash.finalValue, 10'000.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Lissage : ratio brut 0,8 sur 4 barres puis 1,5 — avec smoothLookback = 3,
//  la SMA ne dépasse 1,0 qu'à la barre 5 (moyenne {0,8;1,5;1,5} ≈ 1,27) :
//  la sortie est RETARDÉE d'une barre par rapport au brut, warmup 2 barres.
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, SmoothingDelaysRegimeFlip) {
    //             i :   0     1     2     3     4     5     6
    const std::vector<double> qqq = {100, 101, 102, 103, 104, 105, 106};
    const std::vector<double> vix = { 16,  16,  16,  16,  30,  30,  30};
    const std::vector<double> v3m = { 20,  20,  20,  20,  20,  20,  20};
    // ratio brut : 0,8 (i=0..3) puis 1,5 (i=4..6).
    // SMA(3) : i=2,3 → 0,8 ; i=4 → (0,8+0,8+1,5)/3 ≈ 1,033 > 1 déjà ;
    //          on vérifie donc le RETARD à l'ENTRÉE : warmup i<2 → cash,
    //          entrée à i=2 (et pas i=0 comme en brut), sortie à i=4.
    VixTermRegimeConfig c = cfg0();
    c.smoothLookback = 3;
    const auto r = VixTermRegimeBacktester::fromAxis(
        c, axisQqqVixV3m(dates(qqq.size()), qqq, vix, v3m)).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.trades[0].buyDate,  dates(qqq.size())[2]);   // entrée retardée par le warmup
    EXPECT_EQ(r.trades[0].sellDate, dates(qqq.size())[4]);   // SMA > 1 dès i=4
    EXPECT_EQ(r.trades[0].exitReason, std::string("regime"));
    EXPECT_EQ(r.switchCount, 2);
    // P&L exact : long de i=2 à i=4 (coûts nuls).
    EXPECT_NEAR(r.finalValue, 10'000.0 * qqq[4] / qqq[2], 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Aller-retour à coûts NON nuls : contango → backwardation → contango.
//  Les coûts par côté s'appliquent au switch (pow(1-c, sides)) et l'équité
//  est PLATE en cash entre les deux stints.
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, TwoStintsAndCashIsFlatBetween) {
    VixTermRegimeConfig c = cfg0();
    c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
    const double perSide = 0.001 + (2.0 + 0.5) / 10'000.0;

    //             i :   0     1     2     3     4     5
    const std::vector<double> qqq = {100, 102, 104, 106, 108, 110};
    const std::vector<double> vix = { 16,  16,  30,  30,  16,  16};
    const std::vector<double> v3m = { 20,  20,  20,  20,  20,  20};
    const auto r = VixTermRegimeBacktester::fromAxis(
        c, axisQqqVixV3m(dates(qqq.size()), qqq, vix, v3m)).run();

    // i=0 : entrée (1 côté) ; gagne r0, r1 ; i=2 : sortie (1 côté), cash i=2..3 ;
    // i=4 : ré-entrée (1 côté) ; gagne r4 ; fin : liquidation (1 côté).
    double expected = 10'000.0 * (1.0 - perSide);
    expected *= qqq[2] / qqq[0];
    expected *= (1.0 - perSide);
    expected *= (1.0 - perSide);
    expected *= qqq[5] / qqq[4];
    expected *= (1.0 - perSide);

    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_EQ(r.switchCount, 3);                        // entrée, sortie, ré-entrée
    EXPECT_EQ(r.trades[0].exitReason, std::string("regime"));
    EXPECT_EQ(r.trades[1].exitReason, std::string("fin"));
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    // Équité plate en cash : la barre 3 vaut la barre 2 après coût de sortie.
    ASSERT_EQ(r.equityCurve.size(), qqq.size());
    EXPECT_NEAR(r.equityCurve[3],
                10'000.0 * (1.0 - perSide) * (qqq[2] / qqq[0]) * (1.0 - perSide), 1e-9);
    EXPECT_NEAR(r.equityCurve[3], r.equityCurve[4], 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Fenêtre plus courte que le warmup du lissage → jamais décidable → 0 trade
//  (garde D35 au niveau moteur : « inconnu ≠ calme »).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, RangeShorterThanWarmupNeverTrades) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    VixTermRegimeConfig c = cfg0();
    c.smoothLookback = 10;                              // warmup 9 > 4 barres
    const auto r = VixTermRegimeBacktester::fromAxis(
        c, axisQqqVixV3m(dates(qqq.size()), qqq,
                         {15, 15, 15, 15}, {20, 20, 20, 20})).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_NEAR(r.finalValue, 10'000.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Donnée dégénérée (VIX3M ≤ 0) → ratio non valide → CASH (pas de division
//  par zéro, pas de position sur signal cassé).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, DegenerateSignalStaysInCash) {
    const std::vector<double> qqq = {100, 110, 121, 133.1};
    const auto r = VixTermRegimeBacktester::fromAxis(
        cfg0(), axisQqqVixV3m(dates(qqq.size()), qqq,
                              {15, 15, 15, 15}, {20, 0, 20, 20})).run();
    // i=0 long, i=1 signal cassé → sortie, i=2 ré-entrée, fin liquidation.
    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_EQ(r.trades[0].sellDate, dates(qqq.size())[1]);
    EXPECT_NEAR(r.finalValue,
                10'000.0 * (qqq[1] / qqq[0]) * (qqq[3] / qqq[2]), 1e-9);
}

// ════════════════════════════════════════════════════════════
//  SEULE la série 0 (QQQ) produit du P&L : QQQ plat + signaux mobiles →
//  équité PLATE (les indices de vol ne sont jamais achetés).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeBacktesterUnit, OnlyTradedSeriesAccruesPnl) {
    const std::vector<double> qqq = {100, 100, 100, 100, 100};
    const auto r = VixTermRegimeBacktester::fromAxis(
        cfg0(), axisQqqVixV3m(dates(qqq.size()), qqq,
                              {15, 30, 15, 30, 15}, {20, 20, 20, 20, 20})).run();
    EXPECT_NEAR(r.finalValue, 10'000.0, 1e-9);
    EXPECT_GE(r.switchCount, 3);                        // le régime, lui, a basculé
}
