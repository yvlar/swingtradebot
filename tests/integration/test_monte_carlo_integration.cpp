// ─── Tests d'intégration : MonteCarlo (Sprint 7, item 7.3) ───────────────────
// Distribution bootstrap des trades de la config HÉRITÉE V1 (témoin archivé —
// la prod trade la V2 depuis l'item 9.0 ; son bootstrap est figé dans
// test_strategy_v2_integration). Déterministe, seed 42, 5000 chemins.
// Deux leçons figées :
//   1. Les TRADES pris sont robustes : distribution de retour serrée et
//      positive, P(perte) ≈ 0, drawdown p95 < 1 %. Le bot choisit de bons trades.
//   2. MAIS cela ne capte PAS le coût du cash drag : le p50 (~+37 %) reste très
//      au-dessous du Buy & Hold (~+239 %). Bons trades, trop peu de temps investi
//      — cohérent avec le verdict OOS de l'item 7.1 (D30).
// Chiffres re-figés après le fix D36 (tradeReturns relatif à l'équité au moment
// du trade) : le p50 colle désormais au retour réel (+36,97 % vs +36,14 % réels ;
// l'ancien p50 +43,59 % était gonflé par la division par le capital initial).
#include <gtest/gtest.h>
#include "backtest/MonteCarlo.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

TEST(MonteCarloIntegration, LegacyConfigTradeBootstrapDistribution) {
    Backtester bt(legacyProdConfig(), SWINGBOT_QQQ_CSV, 10'000.0, 0.001, 0.0005);
    auto br = bt.run();
    auto tr = MonteCarlo::tradeReturns(br);
    ASSERT_EQ(tr.size(), 11u);

    auto mc = MonteCarlo::bootstrapTrades(tr, 5000, /*seed=*/42);

    // Distribution figée (déterministe) — sanity D36 : p50 ≈ retour réel (+36,14 %)
    EXPECT_NEAR(mc.p5ReturnPct,  25.9549, 0.05);
    EXPECT_NEAR(mc.p50ReturnPct, 36.9662, 0.05);
    EXPECT_NEAR(mc.p95ReturnPct, 47.1668, 0.05);
    EXPECT_NEAR(mc.p95MaxDrawdownPct, 0.7408, 0.05);
    EXPECT_NEAR(mc.p50ReturnPct, br.totalReturnPct, 2.0);   // l'identité D36 tient

    // Leçon 1 : l'edge de trade est robuste — quasi aucun chemin ne perd
    EXPECT_DOUBLE_EQ(mc.probLossPct, 0.0);
    EXPECT_GT(mc.p5ReturnPct, 0.0);

    // Leçon 2 : même le p95 du bootstrap reste très en deçà du Buy & Hold —
    // le cash drag (pas la qualité des trades) est le problème de la V1.
    EXPECT_LT(mc.p95ReturnPct, br.buyHoldReturnPct * 0.5);
}
