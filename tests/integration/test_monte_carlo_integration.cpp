// ─── Tests d'intégration : MonteCarloBootstrap (Sprint 7.3, D24) ─────────────
// Bootstrap des trades du backtest config PROD sur data/QQQ_TR.csv (série
// total-return de 7.4) : 1000 chemins, graine 42 — reproductible au bit près.
// Valeurs GOLDEN figées le 2026-06-11. Lecture du résultat : le chemin
// historique unique (+4,21 %, max DD quotidien 3,48 %) cache le risque de
// séquence — rééchantillonnés, les mêmes 23 trades donnent p5 = −13,39 % de
// retour et un drawdown médian (granularité trade) de 12,27 %. C'est la
// distribution, pas le chemin unique, qui doit juger la stratégie.
#include <gtest/gtest.h>
#include <string>
#include "backtest/BackTester.hpp"
#include "backtest/MonteCarlo.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

MonteCarloReport runBootstrapOnProdTrades() {
    Backtester bt(prodSwingConfig(),
                  std::string(SWINGBOT_DATA_DIR) + "/QQQ_TR.csv",
                  10'000.0, 0.001);
    const auto r = bt.run();
    // Durée ≈ barres / 252 (annualisation du CAGR des chemins)
    const double years = static_cast<double>(r.equityCurve.size()) / 252.0;
    MonteCarloBootstrap mc(r.trades, 10'000.0, years, /*paths=*/1000,
                           /*seed=*/42);
    return mc.run();
}

} // namespace

// ─── Golden Monte-Carlo (figé le 2026-06-11, Sprint 7.3) ─────────────────────
TEST(MonteCarloIntegration, GoldenDistributionOnProdConfigTrades) {
    const auto rep = runBootstrapOnProdTrades();
    ASSERT_EQ(rep.paths, 1000);
    EXPECT_EQ(rep.seed, 42u);

    // Distribution du retour total : large — le chemin historique (+4,21 %)
    // n'est qu'UN tirage parmi ceux-ci
    EXPECT_NEAR(rep.totalReturnPct.p5,  -13.3937, 1e-3);
    EXPECT_NEAR(rep.totalReturnPct.p50,  20.8474, 1e-3);
    EXPECT_NEAR(rep.totalReturnPct.p95,  73.1219, 1e-3);

    EXPECT_NEAR(rep.cagrPct.p5,  -2.0041, 1e-3);
    EXPECT_NEAR(rep.cagrPct.p50,  2.7017, 1e-3);
    EXPECT_NEAR(rep.cagrPct.p95,  8.0328, 1e-3);

    // Drawdown (granularité trade) : le risque de séquence que le chemin
    // historique unique ne montre pas
    EXPECT_NEAR(rep.maxDrawdownPct.p5,   6.5084, 1e-3);
    EXPECT_NEAR(rep.maxDrawdownPct.p50, 12.2673, 1e-3);
    EXPECT_NEAR(rep.maxDrawdownPct.p95, 24.5810, 1e-3);
}

TEST(MonteCarloIntegration, FixedSeedIsReproducibleEndToEnd) {
    const auto a = runBootstrapOnProdTrades();
    const auto b = runBootstrapOnProdTrades();
    EXPECT_DOUBLE_EQ(a.totalReturnPct.p50, b.totalReturnPct.p50);
    EXPECT_DOUBLE_EQ(a.maxDrawdownPct.p95, b.maxDrawdownPct.p95);
    EXPECT_DOUBLE_EQ(a.cagrPct.p5,         b.cagrPct.p5);
}

// Le rapport console s'affiche sans erreur (p5/p50/p95 lisibles dans ctest)
TEST(MonteCarloIntegration, PrintsPercentileReport) {
    Backtester bt(prodSwingConfig(),
                  std::string(SWINGBOT_DATA_DIR) + "/QQQ_TR.csv",
                  10'000.0, 0.001);
    const auto r = bt.run();
    MonteCarloBootstrap mc(r.trades, 10'000.0,
                           static_cast<double>(r.equityCurve.size()) / 252.0,
                           1000, 42);
    ASSERT_NO_THROW(mc.printReport(mc.run()));
}
