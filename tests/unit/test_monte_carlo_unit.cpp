// ============================================================
//  test_monte_carlo_unit.cpp  —  Tests UNITAIRES
//  Cible : MonteCarlo (backtest/MonteCarlo.hpp)
//  Bootstrap de la séquence de P&L → distribution CAGR/drawdown.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {
TradeRecord tr(double pnlPct) {
    TradeRecord t;
    t.pnlPct = pnlPct;
    t.pnl    = pnlPct;     // dollars non utilisés par MonteCarlo
    t.isWin  = pnlPct > 0;
    return t;
}
std::vector<TradeRecord> mixedTrades() {
    return { tr(+8.0), tr(-3.0), tr(+5.0), tr(-2.0), tr(+12.0), tr(-6.0) };
}
} // namespace

// ════════════════════════════════════════════════════════════
//  Reproductibilité : même graine → mêmes percentiles (au bit près)
// ════════════════════════════════════════════════════════════
TEST(MonteCarloUnit, ReproducibleWithFixedSeed) {
    MonteCarlo mc(10'000.0, /*seed=*/42, /*paths=*/500);
    const auto a = mc.run(mixedTrades(), /*observedYears=*/5.0);
    const auto b = mc.run(mixedTrades(), /*observedYears=*/5.0);

    EXPECT_DOUBLE_EQ(a.cagrP5,  b.cagrP5);
    EXPECT_DOUBLE_EQ(a.cagrP50, b.cagrP50);
    EXPECT_DOUBLE_EQ(a.cagrP95, b.cagrP95);
    EXPECT_DOUBLE_EQ(a.ddP5,    b.ddP5);
    EXPECT_DOUBLE_EQ(a.ddP50,   b.ddP50);
    EXPECT_DOUBLE_EQ(a.ddP95,   b.ddP95);
}

TEST(MonteCarloUnit, DifferentSeedDifferentDistribution) {
    const auto a = MonteCarlo(10'000.0, 42, 500).run(mixedTrades(), 5.0);
    const auto b = MonteCarlo(10'000.0,  7, 500).run(mixedTrades(), 5.0);
    // Au moins un percentile diffère (le tirage est réellement aléatoire).
    EXPECT_TRUE(a.cagrP5 != b.cagrP5 || a.ddP95 != b.ddP95 || a.cagrP95 != b.cagrP95);
}

// ════════════════════════════════════════════════════════════
//  Que des gagnants → drawdown ≈ 0, rendements positifs
// ════════════════════════════════════════════════════════════
TEST(MonteCarloUnit, AllWinningTradesGivePositivePercentiles) {
    std::vector<TradeRecord> wins = { tr(2.0), tr(3.0), tr(1.5), tr(4.0) };
    const auto r = MonteCarlo(10'000.0, 42, 300).run(wins, 3.0);

    EXPECT_DOUBLE_EQ(r.ddP5,  0.0);   // équité monotone croissante
    EXPECT_DOUBLE_EQ(r.ddP50, 0.0);
    EXPECT_DOUBLE_EQ(r.ddP95, 0.0);
    EXPECT_GT(r.cagrP5,  0.0);
    EXPECT_GT(r.cagrP95, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Un seul trade → tous les chemins identiques (dégénéré)
// ════════════════════════════════════════════════════════════
TEST(MonteCarloUnit, SingleTradeIsDegenerate) {
    // Une seule perte de 10 % : chaque chemin = capital × 0,9.
    // Drawdown = 10 % exactement ; CAGR = (0,9^(1/2) - 1) × 100 sur 2 ans.
    const auto r = MonteCarlo(10'000.0, 42, 100).run({ tr(-10.0) }, /*years=*/2.0);

    EXPECT_DOUBLE_EQ(r.ddP5, r.ddP50);
    EXPECT_DOUBLE_EQ(r.ddP50, r.ddP95);
    EXPECT_NEAR(r.ddP50, 10.0, 1e-9);

    const double cagr = (std::pow(0.9, 1.0 / 2.0) - 1.0) * 100.0;
    EXPECT_DOUBLE_EQ(r.cagrP5, r.cagrP95);
    EXPECT_NEAR(r.cagrP50, cagr, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Sizing (item 8o.1, D45) : deployedFraction pondère le rendement
//  porté au portefeuille. Une perte de 10 % SUR LA POSITION avec 50 %
//  de capital déployé = −5 % sur le portefeuille.
// ════════════════════════════════════════════════════════════
TEST(MonteCarloUnit, DeployedFractionScalesPortfolioReturn) {
    TradeRecord t;
    t.pnlPct           = -10.0;   // rendement de la POSITION
    t.deployedFraction = 0.5;     // seule la moitié du capital est investie
    // Chemin dégénéré (1 trade) : capital × (1 + 0,5 × −0,10) = × 0,95.
    const auto r = MonteCarlo(10'000.0, 42, 100).run({ t }, /*years=*/2.0);
    EXPECT_NEAR(r.ddP50, 5.0, 1e-9);   // drawdown = 5 %, pas 10 %
    const double cagr = (std::pow(0.95, 1.0 / 2.0) - 1.0) * 100.0;
    EXPECT_NEAR(r.cagrP50, cagr, 1e-9);

    // Contrôle : le défaut deployedFraction = 1.0 reproduit l'ancien
    // comportement (rétro-compatibilité des TradeRecords synthétiques).
    const auto rFull = MonteCarlo(10'000.0, 42, 100).run({ tr(-10.0) }, 2.0);
    EXPECT_NEAR(rFull.ddP50, 10.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Les percentiles sont toujours ordonnés
// ════════════════════════════════════════════════════════════
TEST(MonteCarloUnit, PercentileOrderingHolds) {
    const auto r = MonteCarlo(10'000.0, 123, 800).run(mixedTrades(), 5.0);
    EXPECT_LE(r.cagrP5,  r.cagrP50);
    EXPECT_LE(r.cagrP50, r.cagrP95);
    EXPECT_LE(r.ddP5,    r.ddP50);
    EXPECT_LE(r.ddP50,   r.ddP95);
    EXPECT_EQ(r.paths,   800u);
}

TEST(MonteCarloUnit, EmptyTradesGivesNullResult) {
    const auto r = MonteCarlo(10'000.0, 42, 500).run({}, 5.0);
    EXPECT_EQ(r.paths, 0u);
    EXPECT_DOUBLE_EQ(r.cagrP50, 0.0);
    EXPECT_DOUBLE_EQ(r.ddP50,   0.0);
}
