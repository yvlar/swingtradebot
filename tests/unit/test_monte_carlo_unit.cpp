// ============================================================
//  test_monte_carlo_unit.cpp  —  Tests UNITAIRES
//  Cible : MonteCarloBootstrap (Sprint 7.3)
//  - percentile : interpolation linéaire (pure)
//  - run        : reproductibilité (graine), ordre des percentiles,
//                 cas dégénérés, distribution dégénérée (trades identiques)
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

TradeRecord t(double pnlPct) {
    TradeRecord tr;
    tr.pnlPct = pnlPct;
    tr.pnl    = pnlPct;       // signe cohérent pour isWin
    tr.isWin  = pnlPct > 0;
    return tr;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  percentile — formule pure
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, PercentileLinearInterpolation) {
    std::vector<double> v{1, 2, 3, 4, 5};
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v,   0.0), 1.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v,  50.0), 3.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v, 100.0), 5.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v,   5.0), 1.2);  // 0.05*4=0.2
}

TEST(MonteCarloUnit, PercentileEmptyAndSingleton) {
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({}, 50.0), 0.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({42.0}, 95.0), 42.0);
}

// ════════════════════════════════════════════════════════════
//  run — reproductibilité, ordre, dégénérescence
// ════════════════════════════════════════════════════════════

// Graine fixée ⇒ résultat strictement identique d'un appel à l'autre.
TEST(MonteCarloUnit, FixedSeedIsReproducible) {
    std::vector<TradeRecord> trades{t(5), t(-3), t(8), t(-2), t(4)};
    MonteCarloBootstrap mc;
    auto a = mc.run(trades, 1000, /*seed=*/42);
    auto b = mc.run(trades, 1000, /*seed=*/42);

    EXPECT_DOUBLE_EQ(a.returnP5,   b.returnP5);
    EXPECT_DOUBLE_EQ(a.returnP50,  b.returnP50);
    EXPECT_DOUBLE_EQ(a.returnP95,  b.returnP95);
    EXPECT_DOUBLE_EQ(a.drawdownP95, b.drawdownP95);
}

// p5 ≤ p50 ≤ p95 pour le retour ET le drawdown.
TEST(MonteCarloUnit, PercentilesAreOrdered) {
    std::vector<TradeRecord> trades{t(10), t(-5), t(7), t(-8), t(3), t(-1)};
    MonteCarloBootstrap mc;
    auto r = mc.run(trades, 2000, /*seed=*/7);

    EXPECT_LE(r.returnP5,    r.returnP50);
    EXPECT_LE(r.returnP50,   r.returnP95);
    EXPECT_LE(r.drawdownP5,  r.drawdownP50);
    EXPECT_LE(r.drawdownP50, r.drawdownP95);
    EXPECT_GE(r.drawdownP5,  0.0);  // un drawdown est toujours ≥ 0
}

// Trades tous identiques (+5 %) : l'ordre n'a aucun effet, chaque chemin est
// le même → distribution dégénérée, tous les percentiles de retour égaux, et
// aucun drawdown (la courbe ne fait que monter).
TEST(MonteCarloUnit, IdenticalTradesGiveDegenerateDistribution) {
    std::vector<TradeRecord> trades{t(5), t(5), t(5), t(5)};
    MonteCarloBootstrap mc;
    auto r = mc.run(trades, 500, /*seed=*/1);

    const double expected = (std::pow(1.05, 4) - 1.0) * 100.0;  // ≈ 21,55 %
    EXPECT_NEAR(r.returnP5,  expected, 1e-9);
    EXPECT_NEAR(r.returnP50, expected, 1e-9);
    EXPECT_NEAR(r.returnP95, expected, 1e-9);
    EXPECT_NEAR(r.drawdownP95, 0.0, 1e-9);
}

TEST(MonteCarloUnit, EmptyTradesOrZeroIterationsReturnZeros) {
    MonteCarloBootstrap mc;
    auto a = mc.run({}, 100, 1);
    EXPECT_EQ(a.sampleSize, 0);
    EXPECT_DOUBLE_EQ(a.returnP50, 0.0);

    auto b = mc.run({t(5)}, 0, 1);
    EXPECT_EQ(b.iterations, 0);
    EXPECT_DOUBLE_EQ(b.returnP50, 0.0);
}
