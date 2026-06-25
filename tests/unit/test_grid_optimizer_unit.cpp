// ============================================================
//  test_grid_optimizer_unit.cpp  —  Tests UNITAIRES
//  Cible : GridOptimizer (backtest/GridOptimizer.hpp)
//  Fonction objectif FACTICE (surface métrique fabriquée) → aucun backtest,
//  on teste le balayage et la sélection de plateau, pas la performance.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include "backtest/GridOptimizer.hpp"

using namespace trading;

// ════════════════════════════════════════════════════════════
//  Le produit cartésien complet est énuméré
// ════════════════════════════════════════════════════════════
TEST(GridOptimizerUnit, EnumeratesFullCartesianProduct) {
    auto obj = [](const SwingConfig&) { return GridScore{1.0, 0.0, 1.0}; };
    GridOptimizer opt(
        /*emaFast   */ {9, 13},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {55, 65},
        /*rsiSellMin*/ {70, 80},
        /*stopLoss  */ {0.05, 0.07},
        /*takeProfit*/ {0.10, 0.15},
        obj);
    // 2 × 1 × 2 × 2 × 2 × 2 = 32
    EXPECT_EQ(opt.evaluate().size(), 32u);
}

// ════════════════════════════════════════════════════════════
//  On retient le PLATEAU, pas le pic isolé
// ════════════════════════════════════════════════════════════
TEST(GridOptimizerUnit, SelectsPlateauNotIsolatedPeak) {
    // Grille 3×3 sur (emaFast, takeProfit), autres axes singletons.
    // Surface métrique : pic isolé à 100 en (emaFast=5, TP=0.1), entouré de
    // faibles ; plateau de 90 au centre. La moyenne de voisinage doit faire
    // gagner le centre (emaFast=10, TP=0.2), pas le pic.
    //          TP=0.1  TP=0.2  TP=0.3
    //  emaF=5   100      1       1
    //  emaF=10   90      90      90
    //  emaF=15    1      90       1
    static const double M[3][3] = {
        {100.0,  1.0,  1.0},
        { 90.0, 90.0, 90.0},
        {  1.0, 90.0,  1.0},
    };
    auto obj = [](const SwingConfig& c) {
        const int i = (c.emaFast == 5) ? 0 : (c.emaFast == 10) ? 1 : 2;
        const int j = static_cast<int>(std::lround(c.takeProfitPct * 10)) - 1;
        return GridScore{M[i][j], /*drawdown=*/0.0, /*alpha=*/1.0};
    };
    GridOptimizer opt(
        {5, 10, 15}, {21}, {55}, {70}, {0.05}, {0.10, 0.20, 0.30}, obj);

    const auto pts = opt.evaluate();
    const auto sel = GridOptimizer::selectRobustPlateau(pts);

    EXPECT_TRUE(sel.passedAlphaFilter);
    EXPECT_EQ(sel.point.cfg.emaFast, 10);                          // centre
    EXPECT_EQ(std::lround(sel.point.cfg.takeProfitPct * 10), 2);   // TP=0.2
    // Surtout PAS le pic isolé.
    EXPECT_FALSE(sel.point.cfg.emaFast == 5 &&
                 std::lround(sel.point.cfg.takeProfitPct * 10) == 1);
}

// ════════════════════════════════════════════════════════════
//  Égalité de plateau → départage sur le drawdown le plus bas
// ════════════════════════════════════════════════════════════
TEST(GridOptimizerUnit, TieBreakOnLowerDrawdown) {
    // Surface plate (métrique = 50 partout) → toutes les moyennes de voisinage
    // sont égales ; seul le drawdown départage. Le point (emaFast=13, TP=0.2)
    // a le drawdown le plus bas et doit être retenu.
    auto obj = [](const SwingConfig& c) {
        const double dd = (c.emaFast == 13 &&
                           std::lround(c.takeProfitPct * 10) == 2) ? 1.0 : 5.0;
        return GridScore{/*metric=*/50.0, dd, /*alpha=*/1.0};
    };
    GridOptimizer opt({9, 13}, {21}, {55}, {70}, {0.05}, {0.10, 0.20}, obj);

    const auto sel = GridOptimizer::selectRobustPlateau(opt.evaluate());
    EXPECT_EQ(sel.point.cfg.emaFast, 13);
    EXPECT_EQ(std::lround(sel.point.cfg.takeProfitPct * 10), 2);
    EXPECT_DOUBLE_EQ(sel.point.score.drawdown, 1.0);
}

// ════════════════════════════════════════════════════════════
//  Filtre dur : aucun alpha > 0 → pas d'edge, sélection non validée
// ════════════════════════════════════════════════════════════
TEST(GridOptimizerUnit, NoEdgeWhenAllAlphaNonPositive) {
    auto obj = [](const SwingConfig&) {
        return GridScore{/*metric=*/10.0, /*drawdown=*/2.0, /*alpha=*/-1.0};
    };
    GridOptimizer opt({9, 13}, {21}, {55}, {70}, {0.05}, {0.10, 0.15}, obj);

    const auto pts = opt.evaluate();
    const auto sel = GridOptimizer::selectRobustPlateau(pts);
    EXPECT_FALSE(sel.passedAlphaFilter);          // verdict : pas d'edge
    EXPECT_EQ(pts.size(), 4u);                     // mais la carte existe
}
