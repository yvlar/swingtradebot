// ============================================================
//  test_walk_forward_unit.cpp  —  Tests UNITAIRES
//  Cible : WalkForward (Sprint 7, item 7.1) — découpage IS/OOS et
//  walk-forward roulant. Logique de fenêtrage sur données synthétiques ;
//  le verdict OOS réel sur QQQ.csv est figé par le test d'intégration.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Série haussière régulière, assez longue pour dépasser le warmup sur chaque
// tranche (emaSlow=21 + rsi=14 + marge) : 200 barres, +0,2 %/barre.
std::vector<Bar> trendingBars(int n = 200, double start = 100.0, double step = 0.002) {
    std::vector<Bar> bars;
    double px = start;
    for (int i = 0; i < n; ++i) {
        Bar b;
        // date ISO croissante (année fixe, jour incrémenté grossièrement)
        int day = i + 1;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "2020-%02d-%02d", 1 + day / 28, 1 + day % 28);
        b.date  = buf;
        b.open  = px; b.high = px * 1.01; b.low = px * 0.99; b.close = px;
        b.volume = 1000;
        bars.push_back(b);
        px *= (1.0 + step);
    }
    return bars;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Split ancré IS / OOS
// ════════════════════════════════════════════════════════════

TEST(WalkForwardUnit, AnchoredSplitProducesTwoContiguousFolds) {
    WalkForward wf(SwingConfig{}, trendingBars());
    auto r = wf.anchoredSplit(0.7);

    ASSERT_EQ(r.folds.size(), 2u);
    EXPECT_EQ(r.folds[0].label, "IS");
    EXPECT_EQ(r.folds[1].label, "OOS");

    // Contiguïté : l'IS finit juste avant le début de l'OOS, et l'union couvre tout
    EXPECT_EQ(r.folds[0].startDate, wf.bars().front().date);
    EXPECT_EQ(r.folds[1].endDate,   wf.bars().back().date);
    EXPECT_LT(r.folds[0].endDate,   r.folds[1].startDate);
}

TEST(WalkForwardUnit, AnchoredSplitFractionMovesTheCut) {
    WalkForward wf(SwingConfig{}, trendingBars());
    auto r50 = wf.anchoredSplit(0.5);
    auto r80 = wf.anchoredSplit(0.8);

    // Plus l'IS est grand, plus la coupure (= début OOS) est tardive
    EXPECT_LT(r50.folds[1].startDate, r80.folds[1].startDate);
}

TEST(WalkForwardUnit, OosAggregatesComputedOnOosFoldOnly) {
    WalkForward wf(SwingConfig{}, trendingBars());
    auto r = wf.anchoredSplit(0.7);

    EXPECT_EQ(r.oosCount, 1);
    // L'agrégat OOS reprend exactement le retour de la tranche OOS
    EXPECT_DOUBLE_EQ(r.oosMeanReturnPct,  r.folds[1].result.totalReturnPct);
    EXPECT_DOUBLE_EQ(r.oosWorstReturnPct, r.folds[1].result.totalReturnPct);
}

// ════════════════════════════════════════════════════════════
//  Walk-forward roulant
// ════════════════════════════════════════════════════════════

TEST(WalkForwardUnit, RollingProducesNContiguousSegmentsCoveringAll) {
    WalkForward wf(SwingConfig{}, trendingBars());
    auto r = wf.rolling(4);

    ASSERT_EQ(r.folds.size(), 4u);
    EXPECT_EQ(r.folds.front().startDate, wf.bars().front().date);
    EXPECT_EQ(r.folds.back().endDate,    wf.bars().back().date);
    // Segments adjacents : fin du k < début du k+1
    for (size_t i = 1; i < r.folds.size(); ++i)
        EXPECT_LT(r.folds[i-1].endDate, r.folds[i].startDate);
    EXPECT_EQ(r.oosCount, 4);   // toutes les tranches comptent en WF fixe
}

// ════════════════════════════════════════════════════════════
//  Cohérence avec le Backtester direct + garde-fous
// ════════════════════════════════════════════════════════════

// Une tranche walk-forward doit donner EXACTEMENT le même résultat qu'un
// Backtester lancé directement sur la même fenêtre de barres.
TEST(WalkForwardUnit, FoldMatchesDirectBacktestOnSameWindow) {
    auto bars = trendingBars();
    WalkForward wf(SwingConfig{}, bars);
    auto r = wf.rolling(1);
    ASSERT_EQ(r.folds.size(), 1u);

    Backtester bt(SwingConfig{}, "<mémoire>", 10'000.0, 0.001, 0.0);
    auto direct = bt.runOn(bars);

    EXPECT_DOUBLE_EQ(r.folds[0].result.finalValue,     direct.finalValue);
    EXPECT_EQ       (r.folds[0].result.totalTrades,    direct.totalTrades);
    EXPECT_DOUBLE_EQ(r.folds[0].result.totalReturnPct, direct.totalReturnPct);
}

TEST(WalkForwardUnit, TooFewBarsYieldsEmptyResult) {
    WalkForward wf(SwingConfig{}, trendingBars(3));
    EXPECT_TRUE(wf.anchoredSplit().folds.empty());
    EXPECT_TRUE(wf.rolling(4).folds.empty());
}

TEST(WalkForwardUnit, RollingRejectsNonPositiveSegments) {
    WalkForward wf(SwingConfig{}, trendingBars());
    EXPECT_TRUE(wf.rolling(0).folds.empty());
}
