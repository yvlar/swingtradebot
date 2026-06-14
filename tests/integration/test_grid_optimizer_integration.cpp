// ─── Tests d'intégration : optimiseur de grille sur QQQ.csv (Sprint 7.2) ─────
// Balaie une PETITE grille EN IN-SAMPLE, retient le choix robuste (plateau),
// puis le juge EN OUT-OF-SAMPLE — la discipline du sprint : on n'optimise
// jamais et on ne juge jamais sur la même période.
#include <gtest/gtest.h>
#include <vector>
#include "backtest/GridOptimizer.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

// Sépare le CSV en IS (2/3) et OOS (1/3).
struct Split { std::vector<Bar> is, oos; };
Split loadSplit() {
    CsvDataFeed feed(SWINGBOT_QQQ_CSV);
    const auto& all = feed.allBars();
    const size_t cut = all.size() * 2 / 3;
    return { std::vector<Bar>(all.begin(), all.begin() + cut),
             std::vector<Bar>(all.begin() + cut, all.end()) };
}

} // namespace

TEST(GridOptimizerIntegration, OptimizesInSampleAndJudgesOutOfSample) {
    auto split = loadSplit();

    GridOptimizer opt(prodSwingConfig(), 10'000.0, 0.001);
    ParamGrid grid;
    grid.emaFast       = {9, 13};
    grid.takeProfitPct = {0.10, 0.15};

    // Carte de sensibilité : 2 × 2 = 4 combinaisons.
    auto mapIS = opt.search(split.is, grid);
    ASSERT_EQ(mapIS.size(), 4u);

    // Choix robuste (plateau) sur l'IS.
    auto chosen = opt.selectRobust(mapIS, grid.axisSizes());
    // Le choix vient bien de la grille.
    EXPECT_TRUE(chosen.config.emaFast == 9 || chosen.config.emaFast == 13);
    EXPECT_TRUE(chosen.config.takeProfitPct == 0.10 ||
                chosen.config.takeProfitPct == 0.15);

    // Jugement HORS échantillon : on rejoue la config retenue sur l'OOS.
    Backtester oosBt(chosen.config, /*csvPath=*/"", 10'000.0, 0.001);
    auto oos = oosBt.runOnBars(split.oos);
    EXPECT_FALSE(oos.equityCurve.empty());  // un vrai backtest OOS a tourné
}
