// ============================================================
//  test_rotation_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : RotationBacktester (backtest/RotationBacktester.hpp)
//  Rotation multi-actifs par régime (Sprint 8-nonies, item 8n.1).
//  Aucun I/O : on exerce les fonctions pures (alignOnCommonDates,
//  rankStrongest) et des runs synthétiques via le seam fromAxis.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/RotationBacktester.hpp"

using namespace trading;

namespace {
// Barre synthétique : seule la clôture est utilisée par la rotation.
Bar bar(const std::string& date, double close) {
    Bar b;
    b.date = date;
    b.open = b.high = b.low = b.close = close;
    b.volume = 0;
    return b;
}
// Axe aligné construit à la main (2 actifs) à partir de dates + closes parallèles.
AlignedAxis axe2(const std::vector<std::string>& dates,
                 const std::vector<double>& c0,
                 const std::vector<double>& c1) {
    AlignedAxis ax;
    ax.dates = dates;
    ax.close = { c0, c1 };
    return ax;
}
} // namespace

// ════════════════════════════════════════════════════════════
//  Alignement sur dates communes
// ════════════════════════════════════════════════════════════
TEST(RotationBacktesterUnit, AlignmentIntersectsCommonDatesOnly) {
    std::vector<Bar> a0 = { bar("2020-01-01",10), bar("2020-01-02",20),
                            bar("2020-01-03",30), bar("2020-01-04",40) };
    std::vector<Bar> a1 = { bar("2020-01-02",21), bar("2020-01-03",31),
                            bar("2020-01-04",41), bar("2020-01-05",51) };
    // a2 n'a PAS le 2020-01-04
    std::vector<Bar> a2 = { bar("2020-01-02",22), bar("2020-01-03",32),
                            bar("2020-01-05",52) };

    const auto ax = alignOnCommonDates({ a0, a1, a2 });
    // Intersection = {2020-01-02, 2020-01-03}
    ASSERT_EQ(ax.dates.size(), 2u);
    EXPECT_EQ(ax.dates[0], "2020-01-02");
    EXPECT_EQ(ax.dates[1], "2020-01-03");
    ASSERT_EQ(ax.assets(), 3u);
    EXPECT_DOUBLE_EQ(ax.close[0][0], 20.0);   // a0 au 01-02
    EXPECT_DOUBLE_EQ(ax.close[1][0], 21.0);
    EXPECT_DOUBLE_EQ(ax.close[2][0], 22.0);
    EXPECT_DOUBLE_EQ(ax.close[0][1], 30.0);   // a0 au 01-03
    EXPECT_DOUBLE_EQ(ax.close[2][1], 32.0);
}

TEST(RotationBacktesterUnit, AlignmentOutputIsAscendingAndEqualLength) {
    std::vector<Bar> a0 = { bar("2020-03-02",1), bar("2020-03-03",2), bar("2020-03-04",3) };
    std::vector<Bar> a1 = { bar("2020-03-02",1), bar("2020-03-03",2), bar("2020-03-04",3) };
    const auto ax = alignOnCommonDates({ a0, a1 });
    ASSERT_EQ(ax.dates.size(), 3u);
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.dates.size());
    for (size_t i = 1; i < ax.dates.size(); ++i)
        EXPECT_LT(ax.dates[i - 1], ax.dates[i]);   // strictement croissantes
}

TEST(RotationBacktesterUnit, AlignmentEmptyWhenNoOverlap) {
    std::vector<Bar> a0 = { bar("2020-01-01",1), bar("2020-01-02",2) };
    std::vector<Bar> a1 = { bar("2020-06-01",1), bar("2020-06-02",2) };
    const auto ax = alignOnCommonDates({ a0, a1 });
    EXPECT_EQ(ax.dates.size(), 0u);
}

// ════════════════════════════════════════════════════════════
//  Classement de régime (main-calculable)
// ════════════════════════════════════════════════════════════
TEST(RotationBacktesterUnit, RankPicksHighestRelativeSma) {
    // forces = { +0.10, +0.05, -0.10, 0.0 } ; éligibles (close>sma) = {0,1}
    EXPECT_EQ(rankStrongest({110,105,90,100}, {100,100,100,100}), 0);
}

TEST(RotationBacktesterUnit, RankAllBearishReturnsCash) {
    EXPECT_EQ(rankStrongest({90,80,99}, {100,100,100}), -1);
}

TEST(RotationBacktesterUnit, RankIneligibleWhenSmaUnknown) {
    // SMA = 0 (warmup / série trop courte) → régime INCONNU, pas « haussier »
    EXPECT_EQ(rankStrongest({110,120}, {0.0,0.0}), -1);
}

TEST(RotationBacktesterUnit, RankTieBreaksByLowestAssetIndex) {
    // Deux forces égales (0.10) → indice le plus bas (scan avec > strict)
    EXPECT_EQ(rankStrongest({110,110}, {100,100}), 0);
}

// ════════════════════════════════════════════════════════════
//  Run complet main-calculé (smaPeriod=3, coûts nuls)
//  A0 mène tôt (i=3,4), A1 mène tard (i=5,6). eq = 10000 * (121/110) *
//  (100/121) * (169/130) * (200/169) = 10000 * 200/143 ≈ 13986.014.
// ════════════════════════════════════════════════════════════
namespace {
RotationConfig cfgSansCout(int smaPeriod = 3) {
    RotationConfig c;
    c.smaPeriod      = smaPeriod;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.0;
    c.slippageBps    = 0.0;
    c.halfSpreadBps  = 0.0;
    return c;
}
AlignedAxis axeHandComputed() {
    std::vector<std::string> d = { "2020-01-01","2020-01-02","2020-01-03","2020-01-06",
                                   "2020-01-07","2020-01-08","2020-01-09","2020-01-10" };
    std::vector<double> a0 = { 100,100,100, 110, 121, 100, 100, 100 };
    std::vector<double> a1 = { 100,100,100, 101, 100, 130, 169, 200 };
    return axe2(d, a0, a1);
}
} // namespace

TEST(RotationBacktesterUnit, RegimeRankingHandComputedOnSyntheticSeries) {
    auto rb = RotationBacktester::fromAxis(cfgSansCout(), axeHandComputed());
    const auto r = rb.run();

    EXPECT_EQ(r.switchCount, 2);                 // cash→A0 (i=3), A0→A1 (i=5)
    ASSERT_EQ(r.trades.size(), 2u);

    // Stint A0 : entrée 01-06 @110, sortie 01-08 @100 (perte)
    EXPECT_EQ(r.trades[0].buyDate,  "2020-01-06");
    EXPECT_EQ(r.trades[0].sellDate, "2020-01-08");
    EXPECT_DOUBLE_EQ(r.trades[0].buyPrice,  110.0);
    EXPECT_DOUBLE_EQ(r.trades[0].sellPrice, 100.0);
    EXPECT_NEAR(r.trades[0].pnlPct, -9.0909, 1e-3);
    EXPECT_FALSE(r.trades[0].isWin);
    EXPECT_EQ(r.trades[0].exitReason, "rotation");
    EXPECT_EQ(r.trades[0].holdDays, 2);
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);

    // Stint A1 : entrée 01-08 @130, sortie 01-10 @200 (gain), liquidation finale
    EXPECT_EQ(r.trades[1].buyDate,  "2020-01-08");
    EXPECT_EQ(r.trades[1].sellDate, "2020-01-10");
    EXPECT_DOUBLE_EQ(r.trades[1].buyPrice,  130.0);
    EXPECT_DOUBLE_EQ(r.trades[1].sellPrice, 200.0);
    EXPECT_NEAR(r.trades[1].pnlPct, 53.8462, 1e-3);
    EXPECT_TRUE(r.trades[1].isWin);
    EXPECT_EQ(r.trades[1].exitReason, "fin");

    EXPECT_NEAR(r.finalValue,      10'000.0 * 200.0 / 143.0, 1e-6);   // ≈ 13986.0140
    EXPECT_NEAR(r.totalReturnPct,  (200.0 / 143.0 - 1.0) * 100.0, 1e-6);
    EXPECT_NEAR(r.pctTimeInvested, 100.0 * 4.0 / 7.0, 1e-6);          // investi i=3..6
}

TEST(RotationBacktesterUnit, ReproducibilitySameDatesSameChoice) {
    auto rb1 = RotationBacktester::fromAxis(cfgSansCout(), axeHandComputed());
    auto rb2 = RotationBacktester::fromAxis(cfgSansCout(), axeHandComputed());
    const auto r1 = rb1.run();
    const auto r2 = rb2.run();
    EXPECT_EQ(r1.switchCount, r2.switchCount);
    ASSERT_EQ(r1.equityCurve.size(), r2.equityCurve.size());
    for (size_t i = 0; i < r1.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(r1.equityCurve[i], r2.equityCurve[i]);   // au bit près
    ASSERT_EQ(r1.trades.size(), r2.trades.size());
    for (size_t i = 0; i < r1.trades.size(); ++i)
        EXPECT_DOUBLE_EQ(r1.trades[i].pnlPct, r2.trades[i].pnlPct);
}

TEST(RotationBacktesterUnit, SwitchCostReducesEquity) {
    auto rbSans = RotationBacktester::fromAxis(cfgSansCout(), axeHandComputed());
    RotationConfig avecCout = cfgSansCout();
    avecCout.commissionPct = 0.001;
    avecCout.slippageBps   = 2.0;
    avecCout.halfSpreadBps = 0.5;
    auto rbAvec = RotationBacktester::fromAxis(avecCout, axeHandComputed());
    const auto sans = rbSans.run();
    const auto avec = rbAvec.run();
    EXPECT_EQ(sans.switchCount, avec.switchCount);   // mêmes bascules
    EXPECT_LT(avec.finalValue, sans.finalValue);     // les coûts rognent l'équité
}

TEST(RotationBacktesterUnit, CashStintsEmitNoTrade) {
    // Série strictement décroissante : le prix est toujours SOUS son SMA (qui
    // retarde au-dessus) → jamais haussier → tout cash, aucun trade.
    std::vector<std::string> d = { "2020-02-03","2020-02-04","2020-02-05","2020-02-06",
                                   "2020-02-07","2020-02-10" };
    std::vector<double> a0 = { 100, 95, 90, 85, 80, 75 };
    std::vector<double> a1 = { 100, 96, 92, 88, 84, 80 };
    auto rb = RotationBacktester::fromAxis(cfgSansCout(), axe2(d, a0, a1));
    const auto r = rb.run();
    EXPECT_EQ(r.trades.size(), 0u);
    EXPECT_EQ(r.switchCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
}
