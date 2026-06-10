// ─── Tests d'intégration : GridOptimizer (Sprint 7, item 7.2) ────────────────
// Optimise emaFast × emaSlow sur l'IN-SAMPLE de QQQ.csv puis évalue le choix en
// OUT-OF-SAMPLE. Leçon figée : même le MEILLEUR jeu de paramètres (pic ou
// plateau) ne bat PAS le Buy & Hold en OOS — preuve que c'est la STRUCTURE de la
// stratégie (TP fixe, vente RSI, pas de filtre de régime) qu'il faut changer
// (Sprint 8), pas les périodes d'EMA. Tuner n'a jamais sauvé une structure cassée.
#include <gtest/gtest.h>
#include "backtest/GridOptimizer.hpp"
#include "backtest/BackTester.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

namespace {
struct Split { std::vector<Bar> is, oos; };
Split loadSplit(double frac = 0.7) {
    CsvDataFeed feed(SWINGBOT_QQQ_CSV);
    auto all = feed.allBars();
    size_t cut = static_cast<size_t>(all.size() * frac);
    return { {all.begin(), all.begin() + cut}, {all.begin() + cut, all.end()} };
}
} // namespace

TEST(GridOptimizerIntegration, EmaGridProducesValidBestAndRobustOnInSample) {
    auto s = loadSplit();
    GridOptimizer opt(ibkrProdConfig(), s.is, 10'000.0, 0.001, 0.0005);
    auto r = opt.optimizeEma({5, 8, 9, 13, 21}, {20, 21, 30, 50, 100});

    ASSERT_GE(r.bestIdx.first, 0);
    ASSERT_GE(r.robustIdx.first, 0);
    EXPECT_TRUE(r.best.valid);
    EXPECT_TRUE(r.robust.valid);
    EXPECT_LT(r.best.emaFast, r.best.emaSlow);     // combinaison légale
    EXPECT_LT(r.robust.emaFast, r.robust.emaSlow);

    // Par définition, le pic brut a un Sharpe ≥ celui du plateau robuste ;
    // l'écart mesure le « coût de la robustesse » (faible ici).
    EXPECT_GE(r.best.sharpe, r.robust.sharpe);

    // Repères documentés (pic IS = EMA 9/21)
    EXPECT_EQ(r.best.emaFast, 9);
    EXPECT_EQ(r.best.emaSlow, 21);
    EXPECT_NEAR(r.best.sharpe, 2.020, 0.02);
}

// LE point : le meilleur jeu de paramètres choisi sur l'IS perd quand même en OOS.
TEST(GridOptimizerIntegration, RobustlyOptimizedParamsStillLoseOutOfSample) {
    auto s = loadSplit();
    GridOptimizer opt(ibkrProdConfig(), s.is, 10'000.0, 0.001, 0.0005);
    auto r = opt.optimizeEma({5, 8, 9, 13, 21}, {20, 21, 30, 50, 100});

    SwingConfig cfg = ibkrProdConfig();
    cfg.emaFast = r.robust.emaFast;
    cfg.emaSlow = r.robust.emaSlow;
    Backtester bt(cfg, "<mémoire>", 10'000.0, 0.001, 0.0005);
    auto oos = bt.runOn(s.oos);

    EXPECT_FALSE(oos.beatsBuyHold);                 // perd en OOS
    EXPECT_LT(oos.totalReturnPct, oos.buyHoldReturnPct * 0.5);  // et de loin
}
