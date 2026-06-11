// ─── Tests d'intégration : GridOptimizer (Sprint 7.2) ────────────────────────
// Grille réduite (12 combos EMA × TP) sur la série TOTAL-RETURN QQQ_TR.csv
// (7.4 — on n'optimise pas sur un actif sans dividendes) : IS = 70 % des
// barres, OOS = le reste. Le choix se fait au PLATEAU sur le score IS (alpha
// net vs B&H), puis est JUGÉ en OOS — jamais l'inverse. Valeurs GOLDEN figées
// le 2026-06-11 : même sur les meilleurs réglages de la grille, l'alpha IS
// reste ≈ −140 pts et le verdict OOS est négatif — la grille ne sauve pas la
// stratégie actuelle, elle MESURE son absence d'edge (c'est son rôle).
#include <gtest/gtest.h>
#include <string>
#include "backtest/GridOptimizer.hpp"

using namespace trading;

namespace {

constexpr size_t kIsEnd  = 1253;   // 70 % des 1790 barres de QQQ_TR.csv
constexpr size_t kOosEnd = 1790;

GridOptimizer::Report runSmallGrid() {
    SwingConfig base;
    ParamGrid grid;
    grid.emaFast       = {5, 9, 13};
    grid.emaSlow       = {21, 30};
    grid.takeProfitPct = {0.10, 0.15};
    GridOptimizer go(base, grid,
                     std::string(SWINGBOT_DATA_DIR) + "/QQQ_TR.csv");
    return go.optimize(0, kIsEnd, kIsEnd, kOosEnd);
}

} // namespace

TEST(GridOptimizerIntegration, GridIsFullyEvaluatedInSample) {
    const auto rep = runSmallGrid();
    ASSERT_EQ(rep.points.size(), 12u);   // 3 × 2 × 2 combinaisons

    // Toutes les combos sont cohérentes (emaFast < emaSlow) et scorées
    for (const auto& p : rep.points) {
        EXPECT_TRUE(p.valid);
        EXPECT_GT(p.robustScore, std::numeric_limits<double>::lowest());
    }
}

// ─── Golden grille (figé le 2026-06-11, Sprint 7.2) ──────────────────────────
TEST(GridOptimizerIntegration, GoldenPlateauSelectionOnQqqTotalReturn) {
    const auto rep = runSmallGrid();

    // Choix au plateau : EMA 5/30, TP 10 % (déterministe sur le CSV commité)
    ASSERT_EQ(rep.bestIndex, 2u);
    const auto& best = rep.points[rep.bestIndex];
    EXPECT_EQ(best.config.emaFast, 5);
    EXPECT_EQ(best.config.emaSlow, 30);
    EXPECT_DOUBLE_EQ(best.config.takeProfitPct, 0.10);

    // Scores IS : tous profondément négatifs — aucune cellule de la grille
    // n'approche le B&H ; le « meilleur » réglage perd ≈ 140 pts d'alpha
    EXPECT_NEAR(best.score,       -139.5320, 1e-3);
    EXPECT_NEAR(best.robustScore, -140.6655, 1e-3);
    for (const auto& p : rep.points) EXPECT_LT(p.score, -100.0);
}

TEST(GridOptimizerIntegration, GoldenChosenConfigIsJudgedOutOfSample) {
    const auto rep = runSmallGrid();

    // Le verdict OOS couvre exactement les barres jamais vues par la grille
    EXPECT_EQ(rep.bestOos.equityCurve.size(), kOosEnd - kIsEnd);
    EXPECT_NEAR(rep.bestOos.totalReturnPct, -2.4829,  1e-3);
    EXPECT_NEAR(rep.bestOos.alpha,          -39.0525, 1e-3);
    EXPECT_EQ(rep.bestOos.totalTrades, 3);
    EXPECT_FALSE(rep.bestOos.beatsBuyHold);

    // L'IS du choix correspond bien au score brut du point retenu
    EXPECT_NEAR(rep.bestIs.alpha, rep.points[rep.bestIndex].score, 1e-9);
}

TEST(GridOptimizerIntegration, SensitivityMapCoversSweptDimensionsOnly) {
    const auto rep = runSmallGrid();
    ASSERT_EQ(rep.sensitivity.size(), 6u);

    // EMA rapide/lente : sensibles ; TP : sans effet ici (les trades sortent
    // par signal/trailing avant d'atteindre +10 %) ; dimensions non balayées
    // (RSI, SL) : nulles par construction
    EXPECT_NEAR(rep.sensitivity[0], 1.423, 1e-2);
    EXPECT_NEAR(rep.sensitivity[1], 2.039, 1e-2);
    EXPECT_DOUBLE_EQ(rep.sensitivity[2], 0.0);
    EXPECT_DOUBLE_EQ(rep.sensitivity[3], 0.0);
    EXPECT_DOUBLE_EQ(rep.sensitivity[4], 0.0);
    EXPECT_DOUBLE_EQ(rep.sensitivity[5], 0.0);
}

// Le rapport console s'affiche sans erreur (carte + choix + verdict OOS)
TEST(GridOptimizerIntegration, PrintsSensitivityAndOosVerdict) {
    SwingConfig base;
    ParamGrid grid;
    grid.emaFast = {5, 9, 13};
    grid.emaSlow = {21, 30};
    grid.takeProfitPct = {0.10, 0.15};
    GridOptimizer go(base, grid,
                     std::string(SWINGBOT_DATA_DIR) + "/QQQ_TR.csv");
    const auto rep = go.optimize(0, kIsEnd, kIsEnd, kOosEnd);
    ASSERT_NO_THROW(go.printReport(rep));
}
