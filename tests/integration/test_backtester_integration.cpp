// ─── Tests d'intégration : Backtester — golden de non-régression ─────────────
// Item 17 (ROADMAP.md) : valeurs figées du backtest complet sur QQQ.csv avec la
// SwingConfig par défaut (capital 10 000 $, commission 0,1 %). Toute dérive du
// comportement de trading global (refactor du backtester, indicateurs, stratégie,
// sizing) doit faire échouer ce test. Valeurs consignées dans le changelog de
// ROADMAP.md — ne les mettre à jour QUE pour un changement de comportement
// volontaire et documenté.
#include <gtest/gtest.h>
#include <memory>
#include "backtest/BackTester.hpp"

namespace {

// Chemin du CSV injecté par CMake (SWINGBOT_QQQ_CSV) : les tests s'exécutent
// depuis le répertoire de build, pas depuis la racine du dépôt.
trading::BacktestResult runGoldenBacktest() {
    trading::SwingConfig cfg; // paramètres par défaut de la stratégie
    trading::Backtester bt(cfg, SWINGBOT_QQQ_CSV, 10'000.0, 0.001);
    return bt.run();
}

} // namespace

// ─── Valeurs golden (figées le 2026-06-10, baseline Sprint 3) ─────────────────
TEST(BacktesterIntegration, GoldenPerformanceOnQqqCsv) {
    const auto r = runGoldenBacktest();

    // Métriques monétaires : tolérance serrée (réassociation flottante admise,
    // pas un trade de différence).
    EXPECT_NEAR(r.finalValue,       10967.06482,  0.01);
    EXPECT_NEAR(r.totalReturnPct,   9.6706482,    1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 238.5544199,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,   2.0262368,    1e-4);
    EXPECT_NEAR(r.sharpeRatio,      0.6229235,    1e-4);
}

TEST(BacktesterIntegration, GoldenTradeBreakdownOnQqqCsv) {
    const auto r = runGoldenBacktest();

    // Décompte des trades : exact — un trade en plus ou en moins est une dérive.
    EXPECT_EQ(r.totalTrades,     7);
    EXPECT_EQ(r.winningTrades,   4);
    EXPECT_EQ(r.losingTrades,    3);
    EXPECT_EQ(r.stopLossCount,   0);
    EXPECT_EQ(r.takeProfitCount, 1);
    EXPECT_EQ(r.trailingCount,   1);
    EXPECT_EQ(r.signalCount,     5);

    // Bornes temporelles des trades : détectent un décalage de signal.
    ASSERT_FALSE(r.trades.empty());
    EXPECT_EQ(r.trades.front().buyDate, "2020-10-29");
    EXPECT_EQ(r.trades.back().sellDate, "2026-02-12");

    // Un point d'équité par barre du CSV (1859 lignes - 1 en-tête).
    EXPECT_EQ(r.equityCurve.size(), 1858u);
}
