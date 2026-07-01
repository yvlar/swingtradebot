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

// Golden « config prod » : backteste la config réellement câblée en production
// via sa source unique (ProdConfig.hpp). Depuis le Sprint 7 (item 7.4), la
// config prod EST la config par défaut (voir la décision ci-dessous) ; ce
// golden reste néanmoins distinct pour verrouiller ce fait : si quelqu'un
// re-diverge la prod du défaut, ce test casse tant que les nouvelles valeurs
// n'ont pas été re-backtestées et re-figées.
trading::BacktestResult runProdConfigBacktest() {
    trading::Backtester bt(trading::prodSwingConfig(), SWINGBOT_QQQ_CSV,
                           10'000.0, 0.001);
    return bt.run();
}

} // namespace

// ─── Valeurs golden (re-figées le 2026-07-01, Sprint 8 items 8.3 → 8.5) ──────
// Historique : figées 2026-06-10 (Sprint 3), re-figées 2026-06-11 (Sprint 6.2,
// coûts réalistes), re-figées 2026-06-25 (Sprint 7.4, ré-export total-return),
// re-figées 2026-06-25 (Sprint 8.1, filtre de régime SMA200 — warmup 37 → 201,
// B&H mesuré +226,12 %). L'item 8.2 (take-profit désactivé par défaut) n'a RIEN
// déplacé : 0 sortie take-profit sur ce plein échantillon (contrainte morte).
// RE-FIGÉES à l'item 8.3 (ENTRÉE SUR LA FORCE, D26/T3 — rsiBuyMax 55 → 100) :
// 20 trades, retour in-sample −4,52 %. RE-FIGÉES à l'item 8.4 (VENTE RSI GATEE
// PAR LE RÉGIME, D26/T2) : mêmes 20 trades, 4 sorties « signal » devenues
// trailing (les gagnants courent), retour +0,02 %. RE-FIGÉES ICI à l'item 8.5
// (RE-ENTRÉE SUR RÉGIME, T4 — regimeReentry par défaut) :
//   • À plat, régime haussier et prix > EMAs → ré-entrée sans attendre un
//     croisement : 22 trades (13 G / 9 P), 1re entrée dès 2019-10-18 (le bot
//     n'attend plus le 1er croisement post-warmup), dernière VENTE 2022-02-07 —
//     la position finale reste OUVERTE jusqu'au bout de la série (le régime
//     tient), c'est exactement l'effet « rester avec la tendance » recherché.
//   • Retour total IN-SAMPLE +0,02 → +18,70 % (Sharpe 0,72, max DD 6,55 %).
//   • Acceptation 8.5 jugée en OOS et SATISFAITE (% temps investi 30,65 →
//     54,02, alpha OOS −13,11 → −10,03 — voir test_strategy_v2_integration.cpp).
//   • B&H inchangé (+226,12 %) : le warmup ne bouge pas (SMA200 inchangée).
// Ces valeurs sont un VERROU de non-régression, pas une preuve d'edge :
// l'alpha OOS reste négatif (−10,03) → pas de déploiement (DoD Sprint 8).
TEST(BacktesterIntegration, GoldenPerformanceOnQqqCsv) {
    const auto r = runGoldenBacktest();

    // Métriques monétaires : tolérance serrée (réassociation flottante admise,
    // pas un trade de différence).
    EXPECT_NEAR(r.finalValue,       11869.79650,  0.01);
    EXPECT_NEAR(r.totalReturnPct,    18.6979650,  1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 226.1234730,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,     6.5477183,  1e-4);
    EXPECT_NEAR(r.sharpeRatio,        0.7245712,  1e-4);
}

TEST(BacktesterIntegration, GoldenTradeBreakdownOnQqqCsv) {
    const auto r = runGoldenBacktest();

    // Décompte des trades : exact — un trade en plus ou en moins est une dérive.
    EXPECT_EQ(r.totalTrades,    22);
    EXPECT_EQ(r.winningTrades,  13);
    EXPECT_EQ(r.losingTrades,    9);
    EXPECT_EQ(r.stopLossCount,   0);
    EXPECT_EQ(r.takeProfitCount, 0);
    EXPECT_EQ(r.trailingCount,  21);
    EXPECT_EQ(r.signalCount,     1);

    // Bornes temporelles des trades : détectent un décalage de signal. La
    // dernière vente date de 2022 car la position finale reste ouverte jusqu'à
    // la fin de la série (seuls les trades CLÔTURÉS sont listés).
    ASSERT_FALSE(r.trades.empty());
    EXPECT_EQ(r.trades.front().buyDate, "2019-10-18");
    EXPECT_EQ(r.trades.back().sellDate, "2022-02-07");

    // Un point d'équité par barre du CSV total-return (1791 lignes - 1 en-tête).
    EXPECT_EQ(r.equityCurve.size(), 1790u);
}

// ─── Golden config PROD = config DÉFAUT (Décision Sprint 7, item 7.4) ────────
// Sur données total-return honnêtes, l'ancienne config prod (EMA 13/21, RSI
// 65/80, SL 7 %, TP 15 %, minHold 2) s'est révélée INFÉRIEURE au défaut (voir
// ProdConfig.hpp). Décision retenue : aligner la prod sur le défaut validé.
// Les valeurs golden prod sont donc IDENTIQUES au défaut ci-dessus.
TEST(BacktesterIntegration, GoldenProdConfigPerformanceOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_NEAR(r.finalValue,       11869.79650,  0.01);
    EXPECT_NEAR(r.totalReturnPct,    18.6979650,  1e-4);
    EXPECT_NEAR(r.buyHoldReturnPct, 226.1234730,  1e-4);
    EXPECT_NEAR(r.maxDrawdownPct,     6.5477183,  1e-4);
    EXPECT_NEAR(r.sharpeRatio,        0.7245712,  1e-4);
}

TEST(BacktesterIntegration, GoldenProdConfigTradeBreakdownOnQqqCsv) {
    const auto r = runProdConfigBacktest();

    EXPECT_EQ(r.totalTrades,    22);
    EXPECT_EQ(r.winningTrades,  13);
    EXPECT_EQ(r.losingTrades,    9);
    EXPECT_EQ(r.stopLossCount,   0);
    EXPECT_EQ(r.takeProfitCount, 0);
    EXPECT_EQ(r.trailingCount,  21);
    EXPECT_EQ(r.signalCount,     1);

    ASSERT_FALSE(r.trades.empty());
    EXPECT_EQ(r.trades.front().buyDate, "2019-10-18");
    EXPECT_EQ(r.trades.back().sellDate, "2022-02-07");

    EXPECT_EQ(r.equityCurve.size(), 1790u);
}

// ─── Côte à côte : la config prod est désormais le défaut validé ─────────────
// Affiche la comparaison et VERROUILLE la décision du Sprint 7 (item 7.4) :
// après mesure total-return honnête, la prod a été alignée sur le défaut, donc
// les deux configs produisent EXACTEMENT le même résultat. Si ce test casse
// (prod ≠ défaut), c'est que la prod a re-divergé : ouvrir une « Décision
// requise » dans ROADMAP.md et re-valider la nouvelle config en OOS (Sprint 7)
// avant tout déploiement.
TEST(BacktesterIntegration, ProdConfigMatchesValidatedDefaultConfig) {
    const auto def  = runGoldenBacktest();
    const auto prod = runProdConfigBacktest();

    auto ligne = [](const std::string& label, double d, double p) {
        std::ostringstream s;
        s << "  " << std::left << std::setw(22) << label
          << std::right << std::fixed << std::setprecision(2)
          << std::setw(12) << d << std::setw(12) << p;
        return s.str();
    };
    std::cout << "  Golden QQQ.csv (TR)    [defaut]      [prod]\n"
              << ligne("Retour total (%)",   def.totalReturnPct,   prod.totalReturnPct)   << "\n"
              << ligne("Buy & Hold (%)",     def.buyHoldReturnPct, prod.buyHoldReturnPct) << "\n"
              << ligne("Alpha vs B&H (pts)", def.alpha,            prod.alpha)            << "\n"
              << ligne("Max drawdown (%)",   def.maxDrawdownPct,   prod.maxDrawdownPct)   << "\n"
              << ligne("Sharpe",             def.sharpeRatio,      prod.sharpeRatio)      << "\n"
              << ligne("Trades",             def.totalTrades,      prod.totalTrades)      << "\n";

    // Prod == défaut, au centime et au trade près (décision Sprint 7).
    EXPECT_NEAR(prod.totalReturnPct, def.totalReturnPct, 1e-9);
    EXPECT_NEAR(prod.sharpeRatio,    def.sharpeRatio,    1e-9);
    EXPECT_NEAR(prod.finalValue,     def.finalValue,     1e-6);
    EXPECT_EQ(prod.totalTrades,      def.totalTrades);

    // Aucune des deux ne bat le Buy & Hold — la rentabilité reste l'objet du
    // Sprint 8 (refonte de la stratégie pour capter la tendance).
    EXPECT_LT(def.totalReturnPct, def.buyHoldReturnPct);
}
