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

// ════════════════════════════════════════════════════════════
//  Sprint 8-bis, item 8b.1 : axes smaTrendPeriod / trailingStopPct
// ════════════════════════════════════════════════════════════

// Les deux nouveaux axes participent au produit cartésien et chaque combo
// porte bien les valeurs balayées.
TEST(GridOptimizerUnit, EnumeratesExtendedAxesCartesianProduct) {
    auto obj = [](const SwingConfig&) { return GridScore{1.0, 0.0, 1.0}; };
    GridOptimizer opt(
        /*emaFast   */ {9, 13},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {100},
        /*rsiSellMin*/ {70},
        /*stopLoss  */ {0.05},
        /*takeProfit*/ {0.0},
        obj, SwingConfig{},
        /*smaTrend  */ {150, 200, 250},
        /*trailing  */ {0.03, 0.05, 0.08});

    const auto pts = opt.evaluate();
    // 2 × 1 × 1 × 1 × 1 × 1 × 3 × 3 = 18
    ASSERT_EQ(pts.size(), 18u);

    // Chaque valeur balayée des nouveaux axes apparaît dans les combos.
    int sma150 = 0, sma250 = 0, tr3 = 0, tr8 = 0;
    for (const auto& p : pts) {
        if (p.cfg.smaTrendPeriod == 150) ++sma150;
        if (p.cfg.smaTrendPeriod == 250) ++sma250;
        if (std::lround(p.cfg.trailingStopPct * 100) == 3) ++tr3;
        if (std::lround(p.cfg.trailingStopPct * 100) == 8) ++tr8;
    }
    EXPECT_EQ(sma150, 6);   // 18 / 3
    EXPECT_EQ(sma250, 6);
    EXPECT_EQ(tr3, 6);
    EXPECT_EQ(tr8, 6);
}

// Rétro-compatibilité : l'appel historique à 6 axes (sans les nouveaux)
// balaie exactement comme avant, et chaque combo porte les valeurs de la
// config de base pour smaTrendPeriod / trailingStopPct.
TEST(GridOptimizerUnit, OmittedNewAxesFallBackToBaseSingleton) {
    auto obj = [](const SwingConfig&) { return GridScore{1.0, 0.0, 1.0}; };
    SwingConfig base;
    base.smaTrendPeriod  = 175;    // valeurs non-défaut pour tracer la source
    base.trailingStopPct = 0.04;
    GridOptimizer opt({9, 13}, {21}, {55, 65}, {70, 80}, {0.05, 0.07},
                      {0.10, 0.15}, obj, base);

    const auto pts = opt.evaluate();
    EXPECT_EQ(pts.size(), 32u);    // comptage historique inchangé
    for (const auto& p : pts) {
        EXPECT_EQ(p.cfg.smaTrendPeriod, 175);
        EXPECT_DOUBLE_EQ(p.cfg.trailingStopPct, 0.04);
    }
}

// La sélection de plateau fonctionne LE LONG des nouveaux axes : pic isolé
// sur (smaTrend=150, trailing=0.03), plateau au centre (200, 0.05).
TEST(GridOptimizerUnit, SelectsPlateauOnTrailingStopAxis) {
    //           tr=0.03  tr=0.05  tr=0.08
    //  sma=150   100       1        1
    //  sma=200    90      90       90
    //  sma=250     1      90        1
    static const double M[3][3] = {
        {100.0,  1.0,  1.0},
        { 90.0, 90.0, 90.0},
        {  1.0, 90.0,  1.0},
    };
    auto obj = [](const SwingConfig& c) {
        const int i = (c.smaTrendPeriod == 150) ? 0
                    : (c.smaTrendPeriod == 200) ? 1 : 2;
        const int j = (std::lround(c.trailingStopPct * 100) == 3) ? 0
                    : (std::lround(c.trailingStopPct * 100) == 5) ? 1 : 2;
        return GridScore{M[i][j], 0.0, 1.0};
    };
    GridOptimizer opt({9}, {21}, {100}, {70}, {0.05}, {0.0},
                      obj, SwingConfig{},
                      {150, 200, 250}, {0.03, 0.05, 0.08});

    const auto sel = GridOptimizer::selectRobustPlateau(opt.evaluate());
    EXPECT_TRUE(sel.passedAlphaFilter);
    EXPECT_EQ(sel.point.cfg.smaTrendPeriod, 200);                     // centre
    EXPECT_EQ(std::lround(sel.point.cfg.trailingStopPct * 100), 5);
    EXPECT_FALSE(sel.point.cfg.smaTrendPeriod == 150 &&
                 std::lround(sel.point.cfg.trailingStopPct * 100) == 3);
}

// La sensibilité par axe (gate de l'item 8b.4) : écart moyen max−min de la
// métrique le long de chaque axe, autres coordonnées fixées. Surface
// fabriquée : la métrique varie de 10 le long de trailing, de 2 le long
// d'emaFast → trailing est l'axe le plus sensible ; les singletons valent 0.
TEST(GridOptimizerUnit, AxisSensitivityRanksMostSensitiveAxis) {
    auto obj = [](const SwingConfig& c) {
        const double vTrail = (std::lround(c.trailingStopPct * 100) == 3) ? 0.0
                            : (std::lround(c.trailingStopPct * 100) == 5) ? 5.0
                                                                          : 10.0;
        const double vEma = (c.emaFast == 9) ? 0.0 : 2.0;
        return GridScore{vTrail + vEma, 0.0, 1.0};
    };
    GridOptimizer opt({9, 13}, {21}, {100}, {70}, {0.05}, {0.0},
                      obj, SwingConfig{},
                      {200}, {0.03, 0.05, 0.08});

    const auto sens = opt.axisSensitivities(opt.evaluate());
    ASSERT_EQ(sens.size(), 8u);   // les 8 axes, dans l'ordre du constructeur

    // Axes balayés : emaFast (0) = 2, trailing (7) = 10 ; singletons = 0.
    EXPECT_DOUBLE_EQ(sens[0], 2.0);
    EXPECT_DOUBLE_EQ(sens[7], 10.0);
    for (size_t ax : {1u, 2u, 3u, 4u, 5u, 6u}) EXPECT_DOUBLE_EQ(sens[ax], 0.0);

    // trailing est STRICTEMENT l'axe le plus sensible (le gate 8b.4).
    for (size_t ax = 0; ax < sens.size(); ++ax) {
        if (ax != 7) { EXPECT_LT(sens[ax], sens[7]); }
    }

    // Les noms d'axes suivent le même ordre (pour les rapports).
    EXPECT_STREQ(GridOptimizer::axisName(7), "trailingStopPct");
    EXPECT_STREQ(GridOptimizer::axisName(6), "smaTrendPeriod");
}
