// ============================================================
//  test_pairs_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : PairsBacktester (backtest/PairsBacktester.hpp)
//  4e famille d'alpha : PAIRS-TRADING market-neutral (Sprint 12, item 12.1).
//  Aucun I/O : on exerce la fonction pure (logSpread) et des runs synthétiques
//  via le seam fromAxis, avec des scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce de conception : avec zWindow = 2, l'écart-type glissant sur une
//  fenêtre de 2 valeurs [a,b] vaut |a−b|/2, donc
//    z[i] = (s[i] − (s[i-1]+s[i])/2) / (|s[i]−s[i-1]|/2) = signe(s[i] − s[i-1]).
//  Le z vaut donc exactement ±1 (signe de la variation du spread) → entrées et
//  sorties parfaitement contrôlables. Avec des coûts nuls, les P&L tombent juste.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/PairsBacktester.hpp"

using namespace trading;

namespace {

// Axe aligné à 2 actifs construit à la main (dates + closes parallèles).
AlignedAxis axis2(const std::vector<std::string>& dates,
                  const std::vector<double>& c0,
                  const std::vector<double>& c1) {
    AlignedAxis ax;
    ax.dates = dates;
    ax.close = { c0, c1 };
    return ax;
}

// Dates ISO séquentielles simples (l'ordre suffit ; les valeurs exactes
// n'importent pas pour les métriques non liées au calendrier).
std::vector<std::string> dates(size_t n) {
    std::vector<std::string> d;
    for (size_t i = 0; i < n; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "2020-01-%02d", static_cast<int>(i + 1));
        d.emplace_back(buf);
    }
    return d;
}

// Config à COÛTS NULS (pour des P&L calculables à la main), zWindow = 2.
PairsConfig cfg0() {
    PairsConfig c;
    c.zWindow = 2; c.entryK = 1.0; c.exitZ = 0.0;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Fonction pure : spread log
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, LogSpreadIsLogRatio) {
    const std::vector<double> p0 = { std::exp(1.0), std::exp(2.0), std::exp(0.5) };
    const std::vector<double> p1 = { 1.0, 1.0, 1.0 };
    const auto s = logSpread(p0, p1);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_NEAR(s[0], 1.0, 1e-9);   // log(e)   − log(1)
    EXPECT_NEAR(s[1], 2.0, 1e-9);   // log(e^2) − log(1)
    EXPECT_NEAR(s[2], 0.5, 1e-9);   // log(e^.5)
}

// Garde de positivité : un prix ≤ 0 répète la dernière valeur valide (pas de NaN).
TEST(PairsBacktesterUnit, LogSpreadGuardsNonPositivePrices) {
    const std::vector<double> p0 = { 100.0, -5.0, 100.0 };
    const std::vector<double> p1 = { 1.0, 1.0, 1.0 };
    const auto s = logSpread(p0, p1);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_NEAR(s[0], std::log(100.0), 1e-9);
    EXPECT_NEAR(s[1], s[0], 1e-12);           // -5 ≤ 0 → répète s[0]
    EXPECT_NEAR(s[2], std::log(100.0), 1e-9);
    EXPECT_TRUE(std::isfinite(s[1]));
}

// ════════════════════════════════════════════════════════════
//  Arité : le pairs-trading exige EXACTEMENT 2 actifs
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(5);
    ax.close = { {100,100,90,99,99}, {100,100,100,100,100}, {50,50,50,50,50} }; // 3 actifs
    const auto r = PairsBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_EQ(r.roundTripCount, 0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_TRUE(r.trades.empty());
}

// ════════════════════════════════════════════════════════════
//  Spread parfaitement plat → σ nulle → aucune décision (garde std==0)
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, FlatSpreadNeverTrades) {
    const auto ax = axis2(dates(6), {100,100,100,100,100,100},
                                    {100,100,100,100,100,100});
    const auto r = PairsBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_EQ(r.roundTripCount, 0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  LONG SPREAD : le spread chute (z=−1) → long P0 / short P1 ; il revient
//  (z=+1) → sortie. P&L 2 jambes COMPOSÉ (pas un ratio de prix), coûts nuls.
//
//  p0 = [100,100,90,99,99], p1 ≡ 100. ratios = [1,1,0.9,0.99,0.99].
//   i=2 : z = signe(log0.9 − 0) = −1 ≤ −1 → LONG. Gagne i2→i3 :
//         r0 = 99/90 − 1 = +0.10, r1 = 0 → pnl barre = +0.5·0.10 = +0.05.
//         eq = 10000 · 1.05 = 10500.
//   i=3 : z = signe(log0.99 − log0.9) = +1 ≥ 0 → SORTIE (retour à la moyenne).
//   Aucune autre séance investie.
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, LongSpreadRoundTripPnlIsExact) {
    const auto ax = axis2(dates(5), {100,100,90,99,99}, {100,100,100,100,100});
    const auto r = PairsBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.roundTripCount, 1);
    EXPECT_NEAR(r.totalReturnPct, 5.0, 1e-9);
    EXPECT_NEAR(r.finalValue, 10'500.0, 1e-6);
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_NEAR(r.trades[0].pnlPct, 5.0, 1e-9);
    EXPECT_TRUE(r.trades[0].isWin);
    EXPECT_EQ(r.trades[0].holdDays, 1);              // entrée i=2, sortie i=3
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
    EXPECT_EQ(r.trades[0].exitReason, std::string("reversion"));
    // Une seule séance réellement investie (i2→i3) sur M−1 = 4 → 25 %.
    EXPECT_NEAR(r.pctTimeInvested, 25.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  SHORT SPREAD : le spread monte (z=+1) → short P0 / long P1 ; P0 chute
//  → le short gagne. p0 = [100,100,110,99,99], p1 ≡ 100.
//   i=2 : z = +1 ≥ 1 → SHORT. Gagne i2→i3 : r0 = 99/110 − 1 = −0.10 →
//         pnl = −1·(0.5·(−0.10)) = +0.05 → eq = 10500.
//   i=3 : z = signe(log0.9 − log1.1) = −1 ≤ 0 → SORTIE.
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, ShortSpreadRoundTripPnlIsExact) {
    const auto ax = axis2(dates(5), {100,100,110,99,99}, {100,100,100,100,100});
    const auto r = PairsBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.roundTripCount, 1);
    EXPECT_NEAR(r.totalReturnPct, 5.0, 1e-9);
    EXPECT_NEAR(r.finalValue, 10'500.0, 1e-6);
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_NEAR(r.trades[0].pnlPct, 5.0, 1e-9);
    EXPECT_TRUE(r.trades[0].isWin);
}

// ════════════════════════════════════════════════════════════
//  Deux allers-retours + garde anti-cash-drag : entre la sortie et la
//  ré-entrée, l'équité est PLATE (market-neutral flat = cash, aucun
//  rendement). Vérifie roundTripCount > 1 (D47) et la liquidation finale.
//
//  p0 = [100,100,90,99,99,90,99], p1 ≡ 100.
//   i=2 LONG, i2→i3 +5 % → 10500 ; i=3 SORTIE ; i=4 σ=0 → plat ;
//   i=5 LONG (spread rechute), i5→i6 +5 % → 11025 ; i=6 liquidation « fin ».
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, TwoRoundTripsAndCashIsFlatBetween) {
    const auto ax = axis2(dates(7), {100,100,90,99,99,90,99},
                                    {100,100,100,100,100,100,100});
    const auto r = PairsBacktester::fromAxis(cfg0(), ax).run();

    ASSERT_EQ(r.roundTripCount, 2);
    EXPECT_GT(r.roundTripCount, 1);                  // D47 : la famille TRADE
    EXPECT_NEAR(r.totalReturnPct, 10.25, 1e-9);      // 1.05·1.05 − 1
    EXPECT_NEAR(r.finalValue, 11'025.0, 1e-6);
    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_NEAR(r.trades[0].pnlPct, 5.0, 1e-9);
    EXPECT_NEAR(r.trades[1].pnlPct, 5.0, 1e-9);      // 10500 → 11025
    EXPECT_EQ(r.trades[1].exitReason, std::string("fin"));  // liquidation forcée
    // 2 séances investies (i2→i3, i5→i6) sur M−1 = 6 → 33.33 %.
    EXPECT_NEAR(r.pctTimeInvested, 100.0 * 2.0 / 6.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Sous-fenêtre : runRange ré-amorce le spread/z sur les seules barres de la
//  fenêtre (anti-leak). Une fenêtre trop courte (< zWindow) ne trade jamais.
// ════════════════════════════════════════════════════════════
TEST(PairsBacktesterUnit, RangeShorterThanWindowNeverTrades) {
    const auto ax = axis2(dates(7), {100,100,90,99,99,90,99},
                                    {100,100,100,100,100,100,100});
    PairsConfig c = cfg0(); c.zWindow = 5;   // warmup 4 > longueur de la sous-fenêtre
    const auto r = PairsBacktester::fromAxis(c, ax).runRange(0, 3);  // 3 barres seulement
    EXPECT_EQ(r.roundTripCount, 0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}
