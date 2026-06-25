// ─── Tests d'intégration : optimiseur de grille en OOS (item 7.2) ────────────
// Petite grille de SwingConfig évaluée par sa MÉTRIQUE OOS (Sharpe moyen du
// walk-forward, item 7.1) — jamais l'in-sample. Acceptation : une carte de
// sensibilité est produite et le point retenu est bien le plateau (moyenne de
// voisinage maximale). La grille est volontairement MINUSCULE (contrainte de
// timeout CI) ; un balayage exhaustif est un outil hors-ligne (le CLI).
#include <gtest/gtest.h>
#include "backtest/GridOptimizer.hpp"
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {
// 2 fenêtres walk-forward sur 1790 barres : IS=900, OOS=400, pas=400
// → s = 0, 400 (s+1300 ≤ 1790).
constexpr size_t IS_BARS  = 900;
constexpr size_t OOS_BARS = 400;
constexpr size_t STEP     = 400;

// Objectif : Sharpe OOS moyen ; filtre alpha = alpha OOS moyen ; départage sur
// le drawdown OOS moyen. C'est la dépendance explicite 7.2 → 7.1.
GridScore objectifOos(const SwingConfig& c) {
    WalkForward wf(c, SWINGBOT_QQQ_CSV, IS_BARS, OOS_BARS, STEP);
    const auto windows = wf.run();
    GridScore s;
    if (windows.empty()) return s;
    double sharpe = 0, alpha = 0, dd = 0;
    for (const auto& w : windows) {
        sharpe += w.oos.sharpeRatio;
        alpha  += w.oos.alpha;
        dd     += w.oos.maxDrawdownPct;
    }
    const double k = static_cast<double>(windows.size());
    s.metric   = sharpe / k;
    s.alpha    = alpha  / k;
    s.drawdown = dd     / k;
    return s;
}
} // namespace

TEST(GridOptimizerIntegration, SmallGridProducesSensitivityMapInOos) {
    GridOptimizer opt(
        /*emaFast   */ {9, 13},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {55, 65},
        /*rsiSellMin*/ {70},
        /*stopLoss  */ {0.05},
        /*takeProfit*/ {0.10, 0.15},
        objectifOos);

    const auto pts = opt.evaluate();
    ASSERT_EQ(pts.size(), 8u);   // 2 × 1 × 2 × 1 × 1 × 2

    // Le point retenu est le plateau : sa moyenne de voisinage est maximale.
    const auto sel = GridOptimizer::selectRobustPlateau(pts);
    for (const auto& p : pts)
        EXPECT_LE(p.neighborhoodAvg, sel.point.neighborhoodAvg + 1e-9);

    // Artefact d'acceptation : la carte de sensibilité. Sur la stratégie
    // actuelle (sans edge), le filtre alpha > 0 ne passe probablement pas —
    // c'est un résultat honnête, pas une erreur du test.
    opt.printSensitivityMap(pts);
}
