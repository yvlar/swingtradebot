// ============================================================
//  test_grid_optimizer_unit.cpp  —  Tests UNITAIRES
//  Cible : GridOptimizer (Sprint 7, item 7.2) — balayage de paramètres et
//  SÉLECTION ROBUSTE (plateau de performance, pas le pic isolé).
//  La logique de sélection est pure → testée sur des matrices de score
//  fabriquées à la main ; le balayage réel sur QQQ est en intégration.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/GridOptimizer.hpp"

using namespace trading;

namespace {
using Cell = GridOptimizer::ScoreCell;
Cell ok(double s)  { return {true,  s}; }
Cell bad()         { return {false, 0.0}; }
} // namespace

// ════════════════════════════════════════════════════════════
//  selectBest — argmax brut du score
// ════════════════════════════════════════════════════════════

TEST(GridOptimizerUnit, SelectBestPicksGlobalMax) {
    std::vector<std::vector<Cell>> m = {
        {ok(0.1), ok(0.2), ok(0.3)},
        {ok(0.4), ok(0.9), ok(0.5)},   // 0.9 au centre
        {ok(0.2), ok(0.3), ok(0.4)},
    };
    auto p = GridOptimizer::selectBest(m);
    EXPECT_EQ(p.first, 1);
    EXPECT_EQ(p.second, 1);
}

TEST(GridOptimizerUnit, SelectBestIgnoresInvalidCells) {
    std::vector<std::vector<Cell>> m = {
        {bad(),   ok(0.2)},
        {ok(0.5), bad()  },
    };
    auto p = GridOptimizer::selectBest(m);
    EXPECT_EQ(p.first, 1);
    EXPECT_EQ(p.second, 0);   // 0.5
}

// ════════════════════════════════════════════════════════════
//  selectRobust — argmax de la moyenne de voisinage 3×3 (plateau)
// ════════════════════════════════════════════════════════════

// LE test du sprint : un pic isolé (0.99 entouré de presque rien) DOIT perdre
// contre un plateau (cellules toutes ~0.6). Un paramètre dont seuls les voisins
// s'effondrent est fragile = sur-ajusté.
TEST(GridOptimizerUnit, SelectRobustPrefersPlateauOverLonelySpike) {
    std::vector<std::vector<Cell>> m = {
        {ok(0.01), ok(0.01), ok(0.01), ok(0.60), ok(0.60), ok(0.60)},
        {ok(0.01), ok(0.99), ok(0.01), ok(0.60), ok(0.62), ok(0.60)},  // spike vs plateau
        {ok(0.01), ok(0.01), ok(0.01), ok(0.60), ok(0.60), ok(0.60)},
    };
    // Le pic brut est le 0.99
    auto best = GridOptimizer::selectBest(m);
    EXPECT_EQ(best.first, 1);
    EXPECT_EQ(best.second, 1);

    // Mais la sélection robuste choisit le PLATEAU (colonnes 3-5, voisinage ~0.6),
    // pas le pic isolé. La cellule exacte dans le plateau dépend d'effets de bord
    // (une cellule au bord a moins de voisins, tous élevés) — ce qui compte est
    // qu'on quitte le pic fragile pour la zone stable.
    auto robust = GridOptimizer::selectRobust(m);
    EXPECT_GE(robust.second, 3);                 // dans le plateau
    EXPECT_FALSE(robust.first == 1 && robust.second == 1);  // PAS le pic
}

// Les cellules invalides ne comptent pas dans la moyenne de voisinage
TEST(GridOptimizerUnit, RobustNeighborhoodSkipsInvalidNeighbors) {
    std::vector<std::vector<Cell>> m = {
        {ok(0.5), bad(),    ok(0.5)},
        {bad(),   ok(0.5),  bad()  },
        {ok(0.5), bad(),    ok(0.5)},
    };
    // Toutes les cellules valides ont le même score → n'importe quel argmax
    // valide convient ; on vérifie juste qu'aucune division par zéro/NaN ne sort
    auto p = GridOptimizer::selectRobust(m);
    EXPECT_TRUE(m[p.first][p.second].valid);
}

TEST(GridOptimizerUnit, EmptyMatrixReturnsSentinel) {
    std::vector<std::vector<Cell>> empty;
    auto p = GridOptimizer::selectBest(empty);
    EXPECT_EQ(p.first, -1);
    EXPECT_EQ(p.second, -1);
}
