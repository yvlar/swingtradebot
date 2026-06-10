// ============================================================
//  test_monte_carlo_unit.cpp  —  Tests UNITAIRES
//  Cible : MonteCarlo (Sprint 7, item 7.3) — bootstrap des trades.
//  Déterministe (seed fixe) → résultats reproductibles et testables.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/MonteCarlo.hpp"
#include <cmath>

using namespace trading;

// ════════════════════════════════════════════════════════════
//  Cas dégénéré : un seul rendement répété → distribution ponctuelle
// ════════════════════════════════════════════════════════════

// 3 trades tous à +10 % : tout ré-échantillon donne (1,1)^3 − 1 = 0,331,
// et comme tous les trades sont gagnants, le drawdown est nul.
TEST(MonteCarloUnit, ConstantPositiveReturnsGiveDeterministicTerminal) {
    auto r = MonteCarlo::bootstrapTrades({0.10, 0.10, 0.10}, /*nPaths=*/500);

    const double expected = std::pow(1.10, 3) - 1.0;
    EXPECT_NEAR(r.p5ReturnPct,  expected * 100.0, 1e-6);
    EXPECT_NEAR(r.p50ReturnPct, expected * 100.0, 1e-6);
    EXPECT_NEAR(r.p95ReturnPct, expected * 100.0, 1e-6);
    EXPECT_NEAR(r.p50MaxDrawdownPct, 0.0, 1e-9);
    EXPECT_DOUBLE_EQ(r.probLossPct, 0.0);
    EXPECT_EQ(r.samples, 500);
}

// Tous perdants → 100 % des chemins finissent en perte, drawdown > 0
TEST(MonteCarloUnit, AllNegativeReturnsAlwaysLose) {
    auto r = MonteCarlo::bootstrapTrades({-0.05, -0.05, -0.05}, 300);
    EXPECT_DOUBLE_EQ(r.probLossPct, 100.0);
    EXPECT_LT(r.p50ReturnPct, 0.0);
    EXPECT_GT(r.p50MaxDrawdownPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Propriétés des percentiles
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, PercentilesAreOrdered) {
    auto r = MonteCarlo::bootstrapTrades({0.20, -0.10, 0.15, -0.08, 0.05, -0.12}, 2000);
    EXPECT_LE(r.p5ReturnPct,  r.p50ReturnPct);
    EXPECT_LE(r.p50ReturnPct, r.p95ReturnPct);
    EXPECT_LE(r.p5MaxDrawdownPct,  r.p50MaxDrawdownPct);
    EXPECT_LE(r.p50MaxDrawdownPct, r.p95MaxDrawdownPct);
    EXPECT_GE(r.probLossPct, 0.0);
    EXPECT_LE(r.probLossPct, 100.0);
}

// ════════════════════════════════════════════════════════════
//  Déterminisme + garde-fous
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, SameSeedSameResult) {
    std::vector<double> tr = {0.1, -0.05, 0.08, -0.03};
    auto a = MonteCarlo::bootstrapTrades(tr, 1000, /*seed=*/123);
    auto b = MonteCarlo::bootstrapTrades(tr, 1000, /*seed=*/123);
    EXPECT_DOUBLE_EQ(a.p5ReturnPct,  b.p5ReturnPct);
    EXPECT_DOUBLE_EQ(a.p50ReturnPct, b.p50ReturnPct);
    EXPECT_DOUBLE_EQ(a.p95MaxDrawdownPct, b.p95MaxDrawdownPct);
}

TEST(MonteCarloUnit, DifferentSeedTypicallyDiffersButStaysOrdered) {
    std::vector<double> tr = {0.2, -0.1, 0.15, -0.12, 0.05};
    auto a = MonteCarlo::bootstrapTrades(tr, 1000, 1);
    auto b = MonteCarlo::bootstrapTrades(tr, 1000, 2);
    // Au moins une borne diffère (deux seeds → deux ré-échantillonnages)
    EXPECT_NE(a.p5ReturnPct, b.p5ReturnPct);
    EXPECT_LE(a.p5ReturnPct, a.p95ReturnPct);
}

TEST(MonteCarloUnit, EmptyTradesYieldsZeroedResult) {
    auto r = MonteCarlo::bootstrapTrades({}, 500);
    EXPECT_EQ(r.samples, 0);
    EXPECT_DOUBLE_EQ(r.p50ReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.p50MaxDrawdownPct, 0.0);
}

TEST(MonteCarloUnit, NonPositivePathsYieldsZeroedResult) {
    auto r = MonteCarlo::bootstrapTrades({0.1, -0.1}, 0);
    EXPECT_EQ(r.samples, 0);
}

// Helper : extraction des rendements de trade RELATIFS À L'ÉQUITÉ
// (pnl$ / capital initial), pas le rendement sur position.
TEST(MonteCarloUnit, TradeReturnsAreEquityRelative) {
    BacktestResult br;
    br.initialCapital = 10'000.0;
    TradeRecord t1; t1.pnl =  1'000.0;   // +10 % de l'équité
    TradeRecord t2; t2.pnl =   -500.0;   // -5 % de l'équité
    br.trades = {t1, t2};
    auto tr = MonteCarlo::tradeReturns(br);
    ASSERT_EQ(tr.size(), 2u);
    EXPECT_DOUBLE_EQ(tr[0], 0.10);
    EXPECT_DOUBLE_EQ(tr[1], -0.05);
}
