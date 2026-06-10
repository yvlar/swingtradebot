// ─── Tests d'intégration : Backtester — golden de non-régression ─────────────
// Item 17 (ROADMAP.md) : valeurs figées du backtest complet sur QQQ.csv avec la
// SwingConfig par défaut (capital 10 000 $, commission 0,1 %). Toute dérive du
// comportement de trading global (refactor du backtester, indicateurs, stratégie,
// sizing) doit faire échouer ce test. Valeurs consignées dans le changelog de
// ROADMAP.md — ne les mettre à jour QUE pour un changement de comportement
// volontaire et documenté.
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include "backtest/BackTester.hpp"
#include "config/ProdConfig.hpp"

namespace {

// Chemin du CSV injecté par CMake (SWINGBOT_QQQ_CSV) : les tests s'exécutent
// depuis le répertoire de build, pas depuis la racine du dépôt.
trading::BacktestResult runGoldenBacktest() {
    trading::SwingConfig cfg; // paramètres par défaut de la stratégie
    trading::Backtester bt(cfg, SWINGBOT_QQQ_CSV, 10'000.0, 0.001);
    return bt.run();
}

// Backtest de la config de PRODUCTION (item 6.1 / D21) : exactement celle
// que main_ibkr.cpp injecte dans le bot, via la source unique ProdConfig.hpp.
trading::BacktestResult runProdConfigBacktest() {
    trading::Backtester bt(trading::ibkrProdConfig(), SWINGBOT_QQQ_CSV,
                           10'000.0, 0.001);
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

// ─── Golden de la config de PRODUCTION (figé le 2026-06-10, item 6.1) ────────
// Avant ce test, la config tradée par main_ibkr.cpp (EMA 13/21, RSI 65/80,
// SL 7 %, TP 15 %, minHold 2) n'était couverte par AUCUN backtest (D21).
// Constat à la date du gel : la config prod SURPERFORME la config par défaut
// (+36,50 % vs +9,67 %, Sharpe 1,82 vs 0,62, 11 trades 10G/1P, drawdown
// comparable) — la branche « Décision requise » (prod sous-performante) ne
// s'ouvre donc pas. Les deux configs restent très en deçà du Buy & Hold
// (+238,55 %) : c'est l'objet des Sprints 7-8.
TEST(BacktesterIntegration, GoldenProdConfigPerformanceOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_NEAR(r.finalValue,       13650.15444,  0.01);
    EXPECT_NEAR(r.totalReturnPct,   36.5015444,   1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 238.5544199,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,   1.9702419,    1e-4);
    EXPECT_NEAR(r.sharpeRatio,      1.8192344,    1e-4);
}

TEST(BacktesterIntegration, GoldenProdConfigTradeBreakdownOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_EQ(r.totalTrades,     11);
    EXPECT_EQ(r.winningTrades,   10);
    EXPECT_EQ(r.losingTrades,    1);
    EXPECT_EQ(r.stopLossCount,   0);
    EXPECT_EQ(r.takeProfitCount, 4);
    EXPECT_EQ(r.trailingCount,   2);
    EXPECT_EQ(r.signalCount,     5);

    ASSERT_FALSE(r.trades.empty());
    EXPECT_EQ(r.trades.front().buyDate, "2019-06-18");
    EXPECT_EQ(r.trades.back().sellDate, "2026-02-03");
    EXPECT_EQ(r.equityCurve.size(), 1858u);
}

// ─── Golden « coûts réalistes » (item 6.2, figé le 2026-06-10) ───────────────
// Config prod + commission 0,1 % + slippage/demi-spread 5 bps par côté.
// Mêmes 11 trades que le golden prod sans slippage : sur QQQ (très liquide),
// 5 bps ne changent aucune décision, ils rabotent le retour de −0,37 pt
// (+36,50 % → +36,14 %). Si un futur edge disparaît sous 5 bps de slippage,
// c'est qu'il n'existait pas.
TEST(BacktesterIntegration, GoldenProdConfigWithRealisticCosts) {
    trading::Backtester bt(trading::ibkrProdConfig(), SWINGBOT_QQQ_CSV,
                           10'000.0, 0.001, /*slippage=*/0.0005);
    const auto r = bt.run();

    EXPECT_NEAR(r.finalValue,     13613.63352, 0.01);
    EXPECT_NEAR(r.totalReturnPct, 36.1363352,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct, 1.9986827,   1e-4);
    EXPECT_NEAR(r.sharpeRatio,    1.8005342,   1e-4);
    EXPECT_EQ(r.totalTrades,   11);
    EXPECT_EQ(r.winningTrades, 10);

    // Le slippage ne peut que coûter : capital final < golden sans slippage
    EXPECT_LT(r.finalValue, 13650.16);
}

// ─── Comparaison côte à côte défaut vs prod (acceptation item 6.1) ───────────
// Affiche les deux goldens l'un contre l'autre et verrouille leur ordre :
// si une modification fait passer la config prod SOUS la config défaut, ce
// test échoue → rouvrir la « Décision requise » de l'item 6.1 (aligner la
// prod sur la meilleure config validée).
TEST(BacktesterIntegration, ProdConfigOutperformsDefaultConfig) {
    const auto def  = runGoldenBacktest();
    const auto prod = runProdConfigBacktest();

    auto ligne = [](const char* nom, const trading::BacktestResult& r) {
        std::cout << "  [6.1] " << nom
                  << " │ retour "   << r.totalReturnPct << " %"
                  << " │ Sharpe "   << r.sharpeRatio
                  << " │ maxDD "    << r.maxDrawdownPct << " %"
                  << " │ trades "   << r.totalTrades
                  << " (" << r.winningTrades << "G/" << r.losingTrades << "P)"
                  << " │ B&H "      << r.buyHoldReturnPct << " %\n";
    };
    ligne("défaut", def);
    ligne("prod  ", prod);

    EXPECT_GT(prod.totalReturnPct, def.totalReturnPct);
    EXPECT_GT(prod.sharpeRatio,    def.sharpeRatio);
    // Rappel permanent de l'écart au Buy & Hold (Sprints 7-8) : aucune des
    // deux configs ne le bat — si un jour c'est le cas, ce test le signalera.
    EXPECT_LT(prod.totalReturnPct, prod.buyHoldReturnPct);
}
