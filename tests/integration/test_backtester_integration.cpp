// ─── Tests d'intégration : Backtester — golden de non-régression ─────────────
// Item 17 (ROADMAP.md) : valeurs figées du backtest complet sur QQQ.csv avec la
// SwingConfig par défaut (capital 10 000 $, commission 0,1 %). Toute dérive du
// comportement de trading global (refactor du backtester, indicateurs, stratégie,
// sizing) doit faire échouer ce test. Valeurs consignées dans le changelog de
// ROADMAP.md — ne les mettre à jour QUE pour un changement de comportement
// volontaire et documenté.
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include "backtest/BackTester.hpp"
#include "strategies/ProdConfig.hpp"

namespace {

// Chemin du CSV injecté par CMake (SWINGBOT_QQQ_CSV) : les tests s'exécutent
// depuis le répertoire de build, pas depuis la racine du dépôt.
trading::BacktestResult runGoldenBacktest() {
    trading::SwingConfig cfg; // paramètres par défaut de la stratégie
    trading::Backtester bt(cfg, SWINGBOT_QQQ_CSV, 10'000.0, 0.001);
    return bt.run();
}

// Golden « config prod » (Sprint 6.1, D21) : backteste la config réellement
// câblée en production via sa source unique (ProdConfig.hpp). Si quelqu'un
// modifie les paramètres live, ce golden casse tant que les nouvelles valeurs
// n'ont pas été re-backtestées et re-figées.
trading::BacktestResult runProdConfigBacktest() {
    trading::Backtester bt(trading::prodSwingConfig(), SWINGBOT_QQQ_CSV,
                           10'000.0, 0.001);
    return bt.run();
}

} // namespace

// ─── Valeurs golden (figées le 2026-06-10, baseline Sprint 3) ─────────────────
// Re-figées le 2026-06-11 (Sprint 6.2, D22) : le modèle de coûts réaliste
// (slippage 2 bps + demi-spread 0,5 bp par côté, défauts du Backtester)
// dégrade chaque fill. Mêmes 7 trades, capital final 10 967,06 → 10 952,89 $
// (−14,17 $, soit −0,14 pt de retour) — coût pur, aucun signal modifié.
TEST(BacktesterIntegration, GoldenPerformanceOnQqqCsv) {
    const auto r = runGoldenBacktest();

    // Métriques monétaires : tolérance serrée (réassociation flottante admise,
    // pas un trade de différence).
    EXPECT_NEAR(r.finalValue,       10952.89128,  0.01);
    EXPECT_NEAR(r.totalReturnPct,   9.5289128,    1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 238.5544199,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,   2.0646787,    1e-4);
    EXPECT_NEAR(r.sharpeRatio,      0.6137703,    1e-4);
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

// ─── Valeurs golden config PROD (figées le 2026-06-11, Sprint 6.1 / D21) ─────
// EMA 13/21, RSI 14 (achat < 65, vente > 80), SL 7 %, TP 15 %, trailing 3 %,
// minHold 2 — la config qui tourne en live (main_ibkr.cpp via ProdConfig.hpp).
// Re-figées le même jour (Sprint 6.2, D22) : avec les coûts réalistes, mêmes
// 11 trades, capital final 13 650,15 → 13 631,89 $ (−18,26 $, −0,18 pt).
TEST(BacktesterIntegration, GoldenProdConfigPerformanceOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_NEAR(r.finalValue,       13631.89398,  0.01);
    EXPECT_NEAR(r.totalReturnPct,   36.3189398,   1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 238.5544199,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,   1.9844613,    1e-4);
    EXPECT_NEAR(r.sharpeRatio,      1.8099057,    1e-4);
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

// ─── Côte à côte : config défaut vs config prod (acceptation 6.1) ────────────
// Affiche la comparaison et VERROUILLE la relation observée : la config prod
// surperforme la config défaut sur QQQ.csv (+36,50 % vs +9,67 %), donc pas de
// « Décision requise » à ce jour. Si ce test casse (la prod passe DERRIÈRE le
// défaut, p. ex. après un changement de coûts ou de paramètres), ouvrir une
// Décision requise dans ROADMAP.md : aligner la prod sur la config validée, ou
// continuer à valider la config prod.
TEST(BacktesterIntegration, ProdConfigComparedSideBySideWithDefaultConfig) {
    const auto def  = runGoldenBacktest();
    const auto prod = runProdConfigBacktest();

    auto ligne = [](const std::string& label, double d, double p) {
        std::ostringstream s;
        s << "  " << std::left << std::setw(22) << label
          << std::right << std::fixed << std::setprecision(2)
          << std::setw(12) << d << std::setw(12) << p;
        return s.str();
    };
    std::cout << "  Golden QQQ.csv         [defaut]      [prod]\n"
              << ligne("Retour total (%)",   def.totalReturnPct,   prod.totalReturnPct)   << "\n"
              << ligne("Buy & Hold (%)",     def.buyHoldReturnPct, prod.buyHoldReturnPct) << "\n"
              << ligne("Alpha vs B&H (pts)", def.alpha,            prod.alpha)            << "\n"
              << ligne("Max drawdown (%)",   def.maxDrawdownPct,   prod.maxDrawdownPct)   << "\n"
              << ligne("Sharpe",             def.sharpeRatio,      prod.sharpeRatio)      << "\n"
              << ligne("Trades",             def.totalTrades,      prod.totalTrades)      << "\n";

    // Les deux configs voient le même marché : B&H identique.
    EXPECT_NEAR(def.buyHoldReturnPct, prod.buyHoldReturnPct, 1e-9);

    // Relation verrouillée (2026-06-11) : prod > défaut en retour total ET en
    // Sharpe, sans drawdown supérieur d'un facteur disqualifiant. Aucune des
    // deux ne bat le Buy & Hold — la rentabilité reste l'objet des Sprints 7-8.
    EXPECT_GT(prod.totalReturnPct, def.totalReturnPct);
    EXPECT_GT(prod.sharpeRatio,    def.sharpeRatio);
    EXPECT_LT(prod.totalReturnPct, prod.buyHoldReturnPct);
}
