// ============================================================
//  test_grid_optimizer_unit.cpp  —  Tests UNITAIRES
//  Cible : GridOptimizer (Sprint 7.2)
//  - selectRobust : retient un PLATEAU, jamais le pic isolé (cœur du sprint)
//  - search       : produit cartésien correct, repli sur la config de base
//  Données synthétiques ; sélection testée sur une carte de scores fabriquée.
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <utility>
#include <algorithm>
#include "backtest/GridOptimizer.hpp"

using namespace trading;

namespace {

// Carte 1-D (seul l'axe 0 varie) : un GridPoint par score, index {i,0,0,0,0,0}.
std::vector<GridPoint> map1D(const std::vector<double>& scores) {
    std::vector<GridPoint> m;
    for (size_t i = 0; i < scores.size(); ++i) {
        GridPoint p;
        p.score = scores[i];
        p.index = {i, 0, 0, 0, 0, 0};
        m.push_back(p);
    }
    return m;
}

std::vector<Bar> flatBars(int n, double start = 100.0) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        char d[32];
        std::snprintf(d, sizeof(d), "2021-%02d-%02d", 1 + i / 28, 1 + i % 28);
        Bar b;
        b.date = d;
        b.open = b.high = b.low = b.close = start + 0.05 * i;
        b.volume = 1000;
        bars.push_back(b);
    }
    return bars;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  selectRobust — le cœur du sprint : plateau > pic isolé
// ════════════════════════════════════════════════════════════

// Le score le plus ÉLEVÉ (18, indice 6) est un pic isolé entouré de zéros ; le
// bloc d'indices 0..4 forme un plateau à 10. La sélection robuste DOIT retenir
// le plateau (score 10), pas le pic — un pic ne généralise pas hors échantillon.
TEST(GridOptimizerUnit, SelectRobustPrefersPlateauOverIsolatedPeak) {
    auto m = map1D({10, 10, 10, 10, 10, 0, 18, 0});
    std::vector<size_t> axisSizes{8, 1, 1, 1, 1, 1};

    auto chosen = GridOptimizer::selectRobust(m, axisSizes);

    EXPECT_DOUBLE_EQ(chosen.score, 10.0);  // sur le plateau, pas le pic (18)
    EXPECT_NE(chosen.index[0], 6u);        // surtout pas le pic isolé

    // Pour mémoire : le pic isolé EST bien le score maximal de la carte.
    double maxScore = 0.0;
    for (const auto& p : m) maxScore = std::max(maxScore, p.score);
    EXPECT_DOUBLE_EQ(maxScore, 18.0);
    EXPECT_GT(maxScore, chosen.score);     // on a délibérément renoncé au max brut
}

TEST(GridOptimizerUnit, SelectRobustEmptyMapReturnsDefault) {
    auto chosen = GridOptimizer::selectRobust({}, {1, 1, 1, 1, 1, 1});
    EXPECT_TRUE(chosen.index.empty());
    EXPECT_DOUBLE_EQ(chosen.score, 0.0);
}

// Un voisinage uniforme : tous les scores égaux → le premier point gagne
// (départage par ordre de rencontre, déterministe).
TEST(GridOptimizerUnit, SelectRobustUniformPicksFirst) {
    auto m = map1D({5, 5, 5, 5});
    auto chosen = GridOptimizer::selectRobust(m, {4, 1, 1, 1, 1, 1});
    EXPECT_EQ(chosen.index[0], 0u);
}

// Régression D31 : un point à score NUL coincé entre un plateau (10) et un pic
// isolé (30) hérite du pic comme voisin. Une MOYENNE de voisinage le ferait
// gagner (un voisin à 30 gonfle la moyenne), voire ferait gagner le point
// adjacent au pic — un choix absurde (score réel nul). Le critère de MINIMUM de
// voisinage l'élimine : un voisin nul tue le minimum. La sélection doit retenir
// le plateau (score 10), jamais le point coincé ni le pic.
TEST(GridOptimizerUnit, SelectRobustRejectsWedgedPointNextToPeak) {
    auto m = map1D({10, 10, 10, 0, 30, 0});
    std::vector<size_t> axisSizes{6, 1, 1, 1, 1, 1};

    auto chosen = GridOptimizer::selectRobust(m, axisSizes);

    EXPECT_DOUBLE_EQ(chosen.score, 10.0);  // sur le plateau
    EXPECT_LT(chosen.index[0], 3u);        // indices 0..2 = le plateau
    EXPECT_NE(chosen.index[0], 4u);        // surtout pas le pic isolé (30)
}

// ════════════════════════════════════════════════════════════
//  search — produit cartésien et repli
// ════════════════════════════════════════════════════════════

// Grille vide → un seul point, la config de base inchangée.
TEST(GridOptimizerUnit, EmptyGridYieldsSingleBasePoint) {
    SwingConfig base;
    GridOptimizer opt(base);
    auto m = opt.search(flatBars(60), ParamGrid{});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m.front().config.emaFast, base.emaFast);
    EXPECT_EQ(m.front().config.emaSlow, base.emaSlow);
}

// 2 (emaFast) × 3 (takeProfit) = 6 combinaisons, toutes distinctes.
TEST(GridOptimizerUnit, SearchEnumeratesCartesianProduct) {
    GridOptimizer opt(SwingConfig{});
    ParamGrid grid;
    grid.emaFast       = {7, 9};
    grid.takeProfitPct = {0.08, 0.10, 0.15};

    auto m = opt.search(flatBars(60), grid);
    ASSERT_EQ(m.size(), 6u);

    // Chaque combinaison de paramètres apparaît une fois.
    std::set<std::pair<int, double>> seen;
    for (const auto& p : m)
        seen.insert({p.config.emaFast, p.config.takeProfitPct});
    EXPECT_EQ(seen.size(), 6u);
}
