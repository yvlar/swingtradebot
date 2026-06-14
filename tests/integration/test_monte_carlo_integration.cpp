// ─── Tests d'intégration : Monte-Carlo sur les trades réels (Sprint 7.3) ─────
// Bootstrap des trades produits par un vrai backtest (config prod sur QQQ.csv).
// Le chemin unique du golden devient une DISTRIBUTION ; on vérifie la
// reproductibilité (graine) et l'ordre des percentiles sur données réelles.
#include <gtest/gtest.h>
#include "backtest/BackTester.hpp"
#include "backtest/MonteCarlo.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

std::vector<TradeRecord> prodTrades() {
    Backtester bt(prodSwingConfig(), SWINGBOT_QQQ_CSV, 10'000.0, 0.001);
    return bt.run().trades;
}

} // namespace

TEST(MonteCarloIntegration, BootstrapsProdTradesReproducibly) {
    const auto trades = prodTrades();
    ASSERT_FALSE(trades.empty());          // le backtest prod produit bien des trades

    MonteCarloBootstrap mc;
    auto a = mc.run(trades, 5000, /*seed=*/2024);
    auto b = mc.run(trades, 5000, /*seed=*/2024);

    // Reproductible au bit près (acceptation 7.3 : graine fixée).
    EXPECT_DOUBLE_EQ(a.returnP50,   b.returnP50);
    EXPECT_DOUBLE_EQ(a.drawdownP95, b.drawdownP95);

    // p5 ≤ p50 ≤ p95, et le drawdown reste positif.
    EXPECT_LE(a.returnP5,    a.returnP50);
    EXPECT_LE(a.returnP50,   a.returnP95);
    EXPECT_LE(a.drawdownP5,  a.drawdownP95);
    EXPECT_GE(a.drawdownP5,  0.0);

    EXPECT_EQ(a.sampleSize, static_cast<int>(trades.size()));
    EXPECT_EQ(a.iterations, 5000);
}
