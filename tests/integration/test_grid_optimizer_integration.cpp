// ─── Tests d'intégration : GridOptimizer (Sprint 7, item 7.2) ────────────────
// Optimise emaFast × emaSlow sur l'IN-SAMPLE de QQQ.csv puis évalue le choix en
// OUT-OF-SAMPLE, sur la config de PROD (V2 depuis l'item 9.0).
//
// Leçon historique (Sprint 7, structure V1) : même le MEILLEUR jeu de paramètres
// ne battait PAS le B&H en OOS — tuner n'a jamais sauvé une structure cassée.
// Depuis la refonte des sorties (Sprint 8), la leçon a BASCULÉ : avec la
// structure V2, les paramètres choisis robustement sur l'IS battent aussi le
// B&H en OOS — la structure était bien le problème, pas les périodes d'EMA.
// (Détail rassurant : pic et plateau IS sont les mêmes qu'en V1 — 9/21 et 13/20 —
// le choix de paramètres est stable à travers la refonte des sorties.)
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

    // Repères documentés (pic IS = EMA 9/21, inchangé V1→V2 ; Sharpe re-figé
    // pour la structure V2 : 2,429 vs 2,020 en V1)
    EXPECT_EQ(r.best.emaFast, 9);
    EXPECT_EQ(r.best.emaSlow, 21);
    EXPECT_NEAR(r.best.sharpe, 2.429, 0.02);
}

// LE point (inversé au Sprint 8) : avec la structure V2, le jeu de paramètres
// choisi robustement sur l'IS bat AUSSI le B&H en OOS. Sous la V1, ce même test
// verrouillait l'inverse (« perd en OOS, et de loin ») — preuve que c'était la
// structure (sorties), pas le tuning, qui détruisait l'edge.
TEST(GridOptimizerIntegration, RobustlyOptimizedParamsAlsoBeatBuyHoldOutOfSample) {
    auto s = loadSplit();
    GridOptimizer opt(ibkrProdConfig(), s.is, 10'000.0, 0.001, 0.0005);
    auto r = opt.optimizeEma({5, 8, 9, 13, 21}, {20, 21, 30, 50, 100});

    // Le plateau robuste reste 13/20 (stable V1→V2)
    EXPECT_EQ(r.robust.emaFast, 13);
    EXPECT_EQ(r.robust.emaSlow, 20);

    SwingConfig cfg = ibkrProdConfig();
    cfg.emaFast = r.robust.emaFast;
    cfg.emaSlow = r.robust.emaSlow;
    Backtester bt(cfg, "<mémoire>", 10'000.0, 0.001, 0.0005);
    auto oos = bt.runOn(s.oos);

    EXPECT_TRUE(oos.beatsBuyHold);                  // bat le B&H en OOS
    EXPECT_NEAR(oos.totalReturnPct,   47.14, 0.05);
    EXPECT_NEAR(oos.buyHoldReturnPct, 41.52, 0.05);
}
