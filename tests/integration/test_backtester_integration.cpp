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

// ─── Golden de la config de PRODUCTION (re-figé le 2026-06-11, item 9.0) ─────
// Depuis l'item 9.0, la prod EST la V2 « suivi de tendance » : sorties refondues
// (8.2 sans take-profit fixe, 8.4 sans vente RSI) + sizing fraction fixe 90 %
// (9.0b). Verdict pleine période : +538,68 % vs B&H +238,55 % — la prod BAT le
// Buy & Hold pour la première fois (in-sample, flatteur ; le verdict honnête est
// l'OOS de test_strategy_v2_integration : +46,5 % vs +39,4 %). Mêmes 11 trades,
// mêmes dates d'entrée que la V1 (l'entrée n'a pas changé — D34) : toute la
// différence vient des sorties (9 trailing / 2 signal, 0 take-profit) et du
// sizing. Témoin V1 archivé : +36,50 %, Sharpe 1,82, 29 % investi
// (legacyProdConfig — cash drag D26/D30).
TEST(BacktesterIntegration, GoldenProdConfigPerformanceOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_NEAR(r.finalValue,       63867.81203,  0.01);
    EXPECT_NEAR(r.totalReturnPct,   538.6781203,  1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 238.5544199,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,   5.9480181,    1e-4);
    EXPECT_NEAR(r.sharpeRatio,      2.2875559,    1e-4);

    // Métriques d'objectif (item 6.4) : cash drag résorbé par les sorties —
    // 62 % du temps investi (vs 29 % en V1), CAGR 29,77 %/an vs ~19 %/an pour QQQ
    EXPECT_NEAR(r.cagrPct,          29.7687637,   1e-4);
    EXPECT_NEAR(r.sortinoRatio,     3.8162601,    1e-4);
    EXPECT_NEAR(r.calmarRatio,      5.0048207,    1e-4);
    EXPECT_NEAR(r.pctTimeInvested,  62.0538166,   1e-4);
    EXPECT_TRUE(r.beatsBuyHold);    // le renversement des Sprints 8-9
}

TEST(BacktesterIntegration, GoldenProdConfigTradeBreakdownOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    // Mêmes 11 trades que la V1 (entrée inchangée — D34) ; sorties refondues :
    // plus aucun take-profit (8.2), le trailing devient la sortie principale.
    EXPECT_EQ(r.totalTrades,     11);
    EXPECT_EQ(r.winningTrades,   10);
    EXPECT_EQ(r.losingTrades,    1);
    EXPECT_EQ(r.stopLossCount,   0);
    EXPECT_EQ(r.takeProfitCount, 0);
    EXPECT_EQ(r.trailingCount,   9);
    EXPECT_EQ(r.signalCount,     2);

    ASSERT_FALSE(r.trades.empty());
    EXPECT_EQ(r.trades.front().buyDate, "2019-06-18");
    EXPECT_EQ(r.trades.back().sellDate, "2026-02-03");
    EXPECT_EQ(r.equityCurve.size(), 1858u);
}

// ─── Golden « coûts réalistes » (item 6.2, re-figé le 2026-06-11) ─────────────
// Config prod (V2) + commission 0,1 % + slippage/demi-spread 5 bps par côté.
// Mêmes 11 trades que le golden prod sans slippage : sur QQQ (très liquide),
// 5 bps ne changent aucune décision, ils rabotent le retour de ~11 pts
// (+538,68 % → +527,45 % — l'écart absolu a grossi avec l'exposition 90 %,
// en relatif c'est toujours ~2 % du capital final). Si un futur edge disparaît
// sous 5 bps de slippage, c'est qu'il n'existait pas.
TEST(BacktesterIntegration, GoldenProdConfigWithRealisticCosts) {
    trading::Backtester bt(trading::ibkrProdConfig(), SWINGBOT_QQQ_CSV,
                           10'000.0, 0.001, /*slippage=*/0.0005);
    const auto r = bt.run();

    EXPECT_NEAR(r.finalValue,     62745.05338, 0.01);
    EXPECT_NEAR(r.totalReturnPct, 527.4505338, 1e-4);
    EXPECT_NEAR(r.maxDrawdownPct, 6.0367926,   1e-4);
    EXPECT_NEAR(r.sharpeRatio,    2.2725704,   1e-4);
    EXPECT_EQ(r.totalTrades,   11);
    EXPECT_EQ(r.winningTrades, 10);

    // Le slippage ne peut que coûter : capital final < golden sans slippage
    EXPECT_LT(r.finalValue, 63867.82);
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
                  << " │ CAGR "     << r.cagrPct << " %"
                  << " │ Sharpe "   << r.sharpeRatio
                  << " │ maxDD "    << r.maxDrawdownPct << " %"
                  << " │ trades "   << r.totalTrades
                  << " (" << r.winningTrades << "G/" << r.losingTrades << "P)"
                  << " │ investi "  << r.pctTimeInvested << " %"
                  << " │ B&H "      << r.buyHoldReturnPct << " %"
                  << " │ bat B&H : " << (r.beatsBuyHold ? "OUI" : "NON") << "\n";
    };
    ligne("défaut", def);
    ligne("prod  ", prod);

    EXPECT_GT(prod.totalReturnPct, def.totalReturnPct);
    EXPECT_GT(prod.sharpeRatio,    def.sharpeRatio);
    // La sentinelle des Sprints 7-8 (« aucune config ne bat le B&H — si un jour
    // c'est le cas, ce test le signalera ») a basculé à l'item 9.0 : la prod (V2)
    // bat désormais le B&H. Verrouillé dans ce sens — une régression sous le B&H
    // rouvrirait la décision de déploiement.
    EXPECT_GT(prod.totalReturnPct, prod.buyHoldReturnPct);
}
