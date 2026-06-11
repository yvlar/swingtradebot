// ============================================================
//  test_grid_optimizer_unit.cpp  —  Tests UNITAIRES
//  Cible : GridOptimizer (Sprint 7.2) — expansion de la grille
//  de paramètres, sélection ROBUSTE (plateau de performance,
//  pas le pic isolé) et carte de sensibilité. Logique pure,
//  scores injectés à la main — aucun backtest ici.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include "backtest/GridOptimizer.hpp"

using namespace trading;

// ════════════════════════════════════════════════════════════
//  expand : produit cartésien de la grille
// ════════════════════════════════════════════════════════════

TEST(GridOptimizerUnit, ExpandGeneratesCartesianProduct) {
    SwingConfig base;
    ParamGrid grid;
    grid.emaFast = {5, 9};
    grid.emaSlow = {21, 30};
    // Dimensions vides → valeur de la config de base (un seul point)

    const auto pts = GridOptimizer::expand(base, grid);
    ASSERT_EQ(pts.size(), 4u);

    // Tous les points héritent des champs non balayés de la base
    for (const auto& p : pts) {
        EXPECT_DOUBLE_EQ(p.config.rsiBuyMax,     base.rsiBuyMax);
        EXPECT_DOUBLE_EQ(p.config.stopLossPct,   base.stopLossPct);
        EXPECT_DOUBLE_EQ(p.config.takeProfitPct, base.takeProfitPct);
    }
    // Les 4 combinaisons (emaFast, emaSlow) sont présentes
    EXPECT_EQ(pts[0].config.emaFast, 5);  EXPECT_EQ(pts[0].config.emaSlow, 21);
    EXPECT_EQ(pts[3].config.emaFast, 9);  EXPECT_EQ(pts[3].config.emaSlow, 30);
}

TEST(GridOptimizerUnit, ExpandMarksIncoherentCombosInvalid) {
    SwingConfig base;
    ParamGrid grid;
    grid.emaFast = {9, 30};
    grid.emaSlow = {21};

    const auto pts = GridOptimizer::expand(base, grid);
    ASSERT_EQ(pts.size(), 2u);
    EXPECT_TRUE (pts[0].valid);   // 9 < 21 : cohérent
    EXPECT_FALSE(pts[1].valid);   // 30 ≥ 21 : croisement EMA sans objet
}

TEST(GridOptimizerUnit, ExpandWithEmptyGridYieldsTheBaseConfigOnly) {
    SwingConfig base;
    const auto pts = GridOptimizer::expand(base, ParamGrid{});
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_TRUE(pts[0].valid);
    EXPECT_EQ(pts[0].config.emaFast, base.emaFast);
}

// ════════════════════════════════════════════════════════════
//  Sélection robuste : le plateau bat le pic isolé
// ════════════════════════════════════════════════════════════

namespace {

// Grille 1-D synthétique : N points le long d'emaFast, scores injectés
std::vector<GridOptimizer::Point> grille1D(const std::vector<double>& scores) {
    SwingConfig base;
    ParamGrid grid;
    for (size_t i = 0; i < scores.size(); ++i)
        grid.emaFast.push_back(2 + static_cast<int>(i));   // < emaSlow=21
    auto pts = GridOptimizer::expand(base, grid);
    for (size_t i = 0; i < scores.size(); ++i) pts[i].score = scores[i];
    return pts;
}

} // namespace

TEST(GridOptimizerUnit, PlateauIsSelectedOverIsolatedPeak) {
    // Pic isolé au point 2 (score 10 entouré de 1 et 0) ; plateau stable aux
    // points 5-8 (≈ 5). Le pic gagne au score brut, le plateau au score
    // robuste — c'est exactement le critère d'acceptation de 7.2
    auto pts = grille1D({0.0, 1.0, 10.0, 1.0, 0.0, 5.0, 5.5, 5.0, 4.8});
    GridOptimizer::computeRobustScores(pts);

    EXPECT_NEAR(pts[2].robustScore, (1.0 + 10.0 + 1.0) / 3.0, 1e-12);
    EXPECT_NEAR(pts[6].robustScore, (5.0 + 5.5 + 5.0) / 3.0,  1e-12);

    const size_t best = GridOptimizer::bestByRobustScore(pts);
    EXPECT_EQ(best, 6u);   // le cœur du plateau, PAS le pic isolé (index 2)
}

TEST(GridOptimizerUnit, RobustScoreSkipsInvalidNeighbours) {
    // Le voisin invalide (combo incohérent, jamais backtesté) ne pollue pas
    // la moyenne du voisinage
    auto pts = grille1D({4.0, 6.0, 0.0});
    pts[2].valid = false;
    GridOptimizer::computeRobustScores(pts);
    EXPECT_NEAR(pts[1].robustScore, (4.0 + 6.0) / 2.0, 1e-12);
}

TEST(GridOptimizerUnit, BestNeverPicksAnInvalidPoint) {
    auto pts = grille1D({1.0, 2.0, 100.0});
    pts[2].valid = false;          // le « meilleur » score est invalide
    GridOptimizer::computeRobustScores(pts);
    const size_t best = GridOptimizer::bestByRobustScore(pts);
    EXPECT_NE(best, 2u);
}

// ════════════════════════════════════════════════════════════
//  Carte de sensibilité des paramètres
// ════════════════════════════════════════════════════════════

TEST(GridOptimizerUnit, SensitivityMapIsolatesTheSensitiveDimension) {
    // Grille 2-D : le score varie fortement le long d'emaFast (dim 0),
    // pas du tout le long d'emaSlow (dim 1)
    SwingConfig base;
    ParamGrid grid;
    grid.emaFast = {5, 9, 13};
    grid.emaSlow = {21, 30};
    auto pts = GridOptimizer::expand(base, grid);
    for (auto& p : pts)
        p.score = p.config.emaFast * 10.0;   // dépend SEULEMENT d'emaFast

    const auto sens = GridOptimizer::sensitivityMap(pts);
    ASSERT_EQ(sens.size(), 6u);              // une entrée par paramètre balayable
    EXPECT_GT(sens[0], 0.0);                 // emaFast : sensible
    EXPECT_DOUBLE_EQ(sens[1], 0.0);          // emaSlow : insensible
    // Dimensions non balayées : sensibilité nulle par construction
    for (size_t d = 2; d < 6; ++d) EXPECT_DOUBLE_EQ(sens[d], 0.0);
}

TEST(GridOptimizerUnit, SensitivityIsMeanAbsoluteStepDelta) {
    auto pts = grille1D({1.0, 3.0, 2.0});    // |Δ| = 2 puis 1 → moyenne 1,5
    const auto sens = GridOptimizer::sensitivityMap(pts);
    EXPECT_NEAR(sens[0], 1.5, 1e-12);
}
