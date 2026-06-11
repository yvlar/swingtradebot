// ============================================================
//  test_monte_carlo_unit.cpp  —  Tests UNITAIRES
//  Cible : MonteCarloBootstrap (Sprint 7.3) — rééchantillonnage
//  des trades avec remise, distribution du retour/CAGR/drawdown
//  (p5/p50/p95), graine fixée = résultats reproductibles.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

TradeRecord trade(double pnlPct) {
    TradeRecord t;
    t.pnlPct = pnlPct;
    t.isWin  = pnlPct > 0;
    return t;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  percentile : rang le plus proche (pur, déterministe)
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, PercentileNearestRank) {
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);   // 1..100

    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v, 5.0),  5.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v, 50.0), 50.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v, 95.0), 95.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile(v, 100.0), 100.0);
}

TEST(MonteCarloUnit, PercentileHandlesUnsortedAndSmallInputs) {
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({3.0, 1.0, 2.0}, 50.0), 2.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({7.0}, 5.0),  7.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({7.0}, 95.0), 7.0);
    EXPECT_DOUBLE_EQ(MonteCarloBootstrap::percentile({}, 50.0), 0.0);
}

// ════════════════════════════════════════════════════════════
//  Reproductibilité : la graine fixe TOUT
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, SameSeedReproducesTheExactSameDistribution) {
    const std::vector<TradeRecord> trades =
        {trade(5.0), trade(-3.0), trade(8.0), trade(-2.0), trade(1.5)};

    MonteCarloBootstrap a(trades, 10'000.0, /*years=*/2.0, /*paths=*/500,
                          /*seed=*/42);
    MonteCarloBootstrap b(trades, 10'000.0, 2.0, 500, 42);
    const auto ra = a.run();
    const auto rb = b.run();

    EXPECT_DOUBLE_EQ(ra.totalReturnPct.p5,  rb.totalReturnPct.p5);
    EXPECT_DOUBLE_EQ(ra.totalReturnPct.p50, rb.totalReturnPct.p50);
    EXPECT_DOUBLE_EQ(ra.totalReturnPct.p95, rb.totalReturnPct.p95);
    EXPECT_DOUBLE_EQ(ra.maxDrawdownPct.p95, rb.maxDrawdownPct.p95);
    EXPECT_DOUBLE_EQ(ra.cagrPct.p50,        rb.cagrPct.p50);
}

TEST(MonteCarloUnit, DifferentSeedChangesTheDraws) {
    const std::vector<TradeRecord> trades =
        {trade(5.0), trade(-3.0), trade(8.0), trade(-2.0), trade(1.5)};

    const auto ra = MonteCarloBootstrap(trades, 10'000.0, 2.0, 500, 42).run();
    const auto rb = MonteCarloBootstrap(trades, 10'000.0, 2.0, 500, 1234).run();
    // Les tirages diffèrent → au moins un percentile bouge
    EXPECT_NE(ra.totalReturnPct.p50, rb.totalReturnPct.p50);
}

// ════════════════════════════════════════════════════════════
//  Propriétés de la distribution
// ════════════════════════════════════════════════════════════

TEST(MonteCarloUnit, PercentilesAreOrdered) {
    const std::vector<TradeRecord> trades =
        {trade(10.0), trade(-8.0), trade(4.0), trade(-1.0), trade(2.0),
         trade(6.0)};
    const auto r = MonteCarloBootstrap(trades, 10'000.0, 3.0, 1000, 7).run();

    EXPECT_LE(r.totalReturnPct.p5,  r.totalReturnPct.p50);
    EXPECT_LE(r.totalReturnPct.p50, r.totalReturnPct.p95);
    EXPECT_LE(r.maxDrawdownPct.p5,  r.maxDrawdownPct.p50);
    EXPECT_LE(r.maxDrawdownPct.p50, r.maxDrawdownPct.p95);
    EXPECT_LE(r.cagrPct.p5,         r.cagrPct.p95);
}

TEST(MonteCarloUnit, AllTradesEqualGivesDegenerateDistribution) {
    // 4 trades identiques : chaque chemin rééchantillonné compose le même
    // gain → la distribution est un point (p5 == p50 == p95), drawdown nul
    const std::vector<TradeRecord> trades =
        {trade(2.0), trade(2.0), trade(2.0), trade(2.0)};
    const auto r = MonteCarloBootstrap(trades, 10'000.0, 1.0, 200, 42).run();

    const double attendu = (std::pow(1.02, 4) - 1.0) * 100.0;
    EXPECT_NEAR(r.totalReturnPct.p5,  attendu, 1e-9);
    EXPECT_NEAR(r.totalReturnPct.p95, attendu, 1e-9);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct.p95, 0.0);
}

TEST(MonteCarloUnit, SingleLosingTradeShowsUpAsDrawdown) {
    const std::vector<TradeRecord> trades = {trade(-10.0)};
    const auto r = MonteCarloBootstrap(trades, 10'000.0, 1.0, 100, 42).run();

    EXPECT_NEAR(r.totalReturnPct.p50, -10.0, 1e-9);
    EXPECT_NEAR(r.maxDrawdownPct.p50,  10.0, 1e-9);
    EXPECT_NEAR(r.cagrPct.p50,        -10.0, 1e-9);   // years = 1
}

TEST(MonteCarloUnit, CagrCompoundsOverTheGivenYears) {
    // +21 % en 2 ans → CAGR = √1,21 − 1 = 10 %
    const std::vector<TradeRecord> trades = {trade(21.0)};
    const auto r = MonteCarloBootstrap(trades, 10'000.0, 2.0, 50, 42).run();
    EXPECT_NEAR(r.cagrPct.p50, 10.0, 1e-9);
}

TEST(MonteCarloUnit, EmptyTradesYieldAnEmptyReport) {
    const auto r = MonteCarloBootstrap({}, 10'000.0, 1.0, 100, 42).run();
    EXPECT_EQ(r.paths, 0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct.p50, 0.0);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct.p95, 0.0);
}
