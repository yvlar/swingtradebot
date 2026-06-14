// ============================================================
//  test_walk_forward_unit.cpp  —  Tests UNITAIRES
//  Cible : WalkForwardValidator (Sprint 7.1, D24)
//  - isOverfit : prédicat pur de sur-ajustement (IS vs OOS)
//  - run       : mécanique de découpage en fenêtres glissantes
//  Données synthétiques : aucune dépendance au CSV de prod.
// ============================================================
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Construit N barres aux dates strictement croissantes et valides (mois 28
// jours pour rester dans 01..28, donc parseables par computeMetrics) ; le
// close suit un palier doux pour que runOnBars ne plante pas.
std::vector<Bar> syntheticBars(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "2020-%02d-%02d", 1 + i / 28, 1 + i % 28);
        Bar b;
        b.date  = buf;
        b.open  = b.high = b.low = b.close = 100.0 + 0.1 * i;
        b.volume = 1000;
        bars.push_back(b);
    }
    return bars;
}

BacktestResult withAlpha(double alpha) {
    BacktestResult r;
    r.alpha = alpha;
    return r;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  isOverfit — le piège que le walk-forward existe pour détecter
// ════════════════════════════════════════════════════════════

// Edge POSITIF en IS qui s'évapore en OOS → sur-ajusté (le cas dangereux).
TEST(WalkForwardUnit, OverfitWhenEdgeInSampleButNotOutOfSample) {
    EXPECT_TRUE(WalkForwardValidator::isOverfit(withAlpha(+15.0), withAlpha(-3.0)));
    EXPECT_TRUE(WalkForwardValidator::isOverfit(withAlpha(+15.0), withAlpha(0.0)));
}

// Edge qui se reproduit en OOS → généralise, pas de sur-ajustement.
TEST(WalkForwardUnit, NotOverfitWhenEdgeHoldsOutOfSample) {
    EXPECT_FALSE(WalkForwardValidator::isOverfit(withAlpha(+15.0), withAlpha(+4.0)));
}

// Stratégie qui perd déjà EN IS : mauvaise, mais pas « sur-ajustée »
// (aucun edge à reproduire). La distinction est importante pour le verdict.
TEST(WalkForwardUnit, NotOverfitWhenNoEdgeInSample) {
    EXPECT_FALSE(WalkForwardValidator::isOverfit(withAlpha(-5.0), withAlpha(-8.0)));
    EXPECT_FALSE(WalkForwardValidator::isOverfit(withAlpha(-5.0), withAlpha(+2.0)));
}

// ════════════════════════════════════════════════════════════
//  run — mécanique des fenêtres glissantes
// ════════════════════════════════════════════════════════════

// 100 barres, IS=20 + OOS=10, pas=10 → fenêtres à start ∈ {0,10,..,70} = 8.
TEST(WalkForwardUnit, WindowCountAndBoundaries) {
    auto bars = syntheticBars(100);
    WalkForwardValidator wf(SwingConfig{});
    auto r = wf.run(bars, /*isBars=*/20, /*oosBars=*/10, /*stepBars=*/10);

    ASSERT_EQ(r.windows.size(), 8u);

    // Première fenêtre : IS = barres [0,20), OOS = [20,30).
    EXPECT_EQ(r.windows.front().isStartDate,  bars[0].date);
    EXPECT_EQ(r.windows.front().isEndDate,    bars[19].date);
    EXPECT_EQ(r.windows.front().oosStartDate, bars[20].date);
    EXPECT_EQ(r.windows.front().oosEndDate,   bars[29].date);

    // Deuxième fenêtre décalée du pas (10).
    EXPECT_EQ(r.windows[1].isStartDate, bars[10].date);
}

// Pas assez de barres pour une seule fenêtre → rapport vide, pas de crash.
TEST(WalkForwardUnit, NoWindowsWhenInsufficientBars) {
    auto bars = syntheticBars(25);
    WalkForwardValidator wf(SwingConfig{});
    auto r = wf.run(bars, /*isBars=*/20, /*oosBars=*/10, /*stepBars=*/10);
    EXPECT_TRUE(r.windows.empty());
    EXPECT_FALSE(r.generalizes);
}

// Paramètres dégénérés (≤ 0) → rapport vide sans boucle infinie.
TEST(WalkForwardUnit, DegenerateParametersReturnEmpty) {
    auto bars = syntheticBars(100);
    WalkForwardValidator wf(SwingConfig{});
    EXPECT_TRUE(wf.run(bars, 0, 10, 10).windows.empty());
    EXPECT_TRUE(wf.run(bars, 20, 0, 10).windows.empty());
    EXPECT_TRUE(wf.run(bars, 20, 10, 0).windows.empty());
}

// Le verdict « généralise » exige alpha OOS moyen > 0 ET zéro fenêtre
// sur-ajustée — on vérifie le branchement sur des fenêtres réelles.
TEST(WalkForwardUnit, ReportAggregatesAreConsistent) {
    auto bars = syntheticBars(100);
    WalkForwardValidator wf(SwingConfig{});
    auto r = wf.run(bars, 20, 10, 10);

    ASSERT_FALSE(r.windows.empty());
    // overfitWindows = nombre de fenêtres marquées overfit
    int counted = 0;
    for (const auto& w : r.windows) if (w.overfit) counted++;
    EXPECT_EQ(counted, r.overfitWindows);
    // generalizes implique aucune fenêtre sur-ajustée
    if (r.generalizes) EXPECT_EQ(r.overfitWindows, 0);
}
