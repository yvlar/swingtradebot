// ─── Tests d'intégration : MonteCarlo (Sprint 7, item 7.3) ───────────────────
// Distribution bootstrap des trades de la config de PRODUCTION (déterministe,
// seed 42, 5000 chemins). Deux leçons figées :
//   1. Les TRADES pris sont robustes : distribution de retour serrée et
//      positive, P(perte) ≈ 0, drawdown p95 ~1 %. Le bot choisit de bons trades.
//   2. MAIS cela ne capte PAS le coût du cash drag : le p50 (~+44 %) reste très
//      au-dessous du Buy & Hold (~+239 %). Bons trades, trop peu de temps investi
//      — cohérent avec le verdict OOS de l'item 7.1.
#include <gtest/gtest.h>
#include "backtest/MonteCarlo.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

TEST(MonteCarloIntegration, ProdConfigTradeBootstrapDistribution) {
    Backtester bt(ibkrProdConfig(), SWINGBOT_QQQ_CSV, 10'000.0, 0.001, 0.0005);
    auto br = bt.run();
    auto tr = MonteCarlo::tradeReturns(br);
    ASSERT_EQ(tr.size(), 11u);

    auto mc = MonteCarlo::bootstrapTrades(tr, 5000, /*seed=*/42);

    // Distribution figée (déterministe)
    EXPECT_NEAR(mc.p5ReturnPct,  29.7872, 0.05);
    EXPECT_NEAR(mc.p50ReturnPct, 43.5873, 0.05);
    EXPECT_NEAR(mc.p95ReturnPct, 56.2244, 0.05);
    EXPECT_NEAR(mc.p95MaxDrawdownPct, 1.0141, 0.05);

    // Leçon 1 : l'edge de trade est robuste — quasi aucun chemin ne perd
    EXPECT_DOUBLE_EQ(mc.probLossPct, 0.0);
    EXPECT_GT(mc.p5ReturnPct, 0.0);

    // Leçon 2 : même le p95 du bootstrap reste très en deçà du Buy & Hold —
    // le cash drag (pas la qualité des trades) est le problème.
    EXPECT_LT(mc.p95ReturnPct, br.buyHoldReturnPct * 0.5);
}
