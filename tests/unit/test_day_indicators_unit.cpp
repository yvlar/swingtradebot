// ============================================================
//  test_day_indicators_unit.cpp  —  Tests UNITAIRES
//  Cible : ATR, VWAP, VolumeOscillator (indicators/DayIndicators.hpp)
//  Item 13 : le fichier n'existait pas — DayTradeStrategy ne compilait
//  que parce qu'aucun TU ne l'incluait.
// ============================================================
#include <gtest/gtest.h>
#include "indicators/DayIndicators.hpp"

using namespace trading;

// ════════════════════════════════════════════════════════════
//  ATR
// ════════════════════════════════════════════════════════════

TEST(DayIndicatorsUnit, AtrThrowsOnNonPositivePeriod) {
    EXPECT_THROW(ATR(0),  std::invalid_argument);
    EXPECT_THROW(ATR(-3), std::invalid_argument);
}

TEST(DayIndicatorsUnit, AtrTooFewPointsReturnsEmpty) {
    ATR atr(14);
    std::vector<double> prices(14, 100.0);  // il faut period+1 points
    EXPECT_TRUE(atr.compute(prices).empty());
}

TEST(DayIndicatorsUnit, AtrIsZeroOnConstantPrices) {
    ATR atr(5);
    std::vector<double> prices(20, 100.0);
    auto v = atr.compute(prices);
    ASSERT_EQ(v.size(), prices.size());
    for (size_t i = 5; i < v.size(); ++i)
        EXPECT_DOUBLE_EQ(v[i], 0.0) << "indice " << i;
}

TEST(DayIndicatorsUnit, AtrEqualsUnitOnUnitMoves) {
    // Variations de ±1 à chaque barre → TR=1 partout → ATR=1 dès le seed
    ATR atr(4);
    std::vector<double> prices;
    for (int i = 0; i < 15; ++i)
        prices.push_back(100.0 + (i % 2));
    auto v = atr.compute(prices);
    ASSERT_EQ(v.size(), prices.size());
    EXPECT_DOUBLE_EQ(v[3], 0.0);  // warmup : zéro avant l'indice `period`
    for (size_t i = 4; i < v.size(); ++i)
        EXPECT_NEAR(v[i], 1.0, 1e-12) << "indice " << i;
}

TEST(DayIndicatorsUnit, AtrWilderSmoothingKnownSequence) {
    // period=2 ; prix 10, 12, 11, 15 → TR = 2, 1, 4
    // seed (indice 2) = (2+1)/2 = 1.5 ; indice 3 = (1.5·1 + 4)/2 = 2.75
    ATR atr(2);
    auto v = atr.compute({10.0, 12.0, 11.0, 15.0});
    ASSERT_EQ(v.size(), 4u);
    EXPECT_DOUBLE_EQ(v[2], 1.5);
    EXPECT_DOUBLE_EQ(v[3], 2.75);
}

// ════════════════════════════════════════════════════════════
//  VWAP
// ════════════════════════════════════════════════════════════

namespace {
Bar makeBar(const std::string& date, double typical, long volume) {
    // high = low = close = prix typique → (H+L+C)/3 = typical
    Bar b;
    b.date   = date;
    b.open   = typical;
    b.high   = typical;
    b.low    = typical;
    b.close  = typical;
    b.volume = volume;
    return b;
}
} // namespace

TEST(DayIndicatorsUnit, VwapComputeIsCumulativeMeanWithoutVolume) {
    VWAP vwap;
    auto v = vwap.compute({10.0, 20.0, 30.0});
    ASSERT_EQ(v.size(), 3u);
    EXPECT_DOUBLE_EQ(v[0], 10.0);
    EXPECT_DOUBLE_EQ(v[1], 15.0);
    EXPECT_DOUBLE_EQ(v[2], 20.0);
}

TEST(DayIndicatorsUnit, VwapWeightsByVolumeWithinSession) {
    VWAP vwap;
    std::vector<Bar> bars{
        makeBar("2024-03-05 09:30", 10.0, 1),
        makeBar("2024-03-05 09:35", 20.0, 3),
    };
    auto v = vwap.computeWithVolume(bars);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_DOUBLE_EQ(v[0], 10.0);
    EXPECT_DOUBLE_EQ(v[1], (10.0 * 1 + 20.0 * 3) / 4.0);  // 17.5
}

TEST(DayIndicatorsUnit, VwapResetsOnNewSession) {
    VWAP vwap;
    std::vector<Bar> bars{
        makeBar("2024-03-05 15:55", 10.0, 100),
        makeBar("2024-03-06 09:30", 50.0, 1),
    };
    auto v = vwap.computeWithVolume(bars);
    ASSERT_EQ(v.size(), 2u);
    // Le gros volume de la veille ne pèse plus : nouvelle session
    EXPECT_DOUBLE_EQ(v[1], 50.0);
}

TEST(DayIndicatorsUnit, VwapFallsBackToTypicalPriceOnZeroVolume) {
    VWAP vwap;
    std::vector<Bar> bars{ makeBar("2024-03-05", 42.0, 0) };
    auto v = vwap.computeWithVolume(bars);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_DOUBLE_EQ(v[0], 42.0);
}

// ════════════════════════════════════════════════════════════
//  VolumeOscillator
// ════════════════════════════════════════════════════════════

TEST(DayIndicatorsUnit, VolumeOscillatorThrowsOnNonPositivePeriod) {
    EXPECT_THROW(VolumeOscillator(0), std::invalid_argument);
}

TEST(DayIndicatorsUnit, VolumeOscillatorNeutralDuringWarmup) {
    VolumeOscillator osc(20);
    std::vector<double> volumes(10, 500.0);  // moins que period
    auto v = osc.compute(volumes);
    ASSERT_EQ(v.size(), volumes.size());
    for (double x : v)
        EXPECT_DOUBLE_EQ(x, 1.0);
}

TEST(DayIndicatorsUnit, VolumeOscillatorRatioVsPriorAverage) {
    // period=2 : moyenne des 2 volumes précédents, barre courante exclue
    VolumeOscillator osc(2);
    auto v = osc.compute({10.0, 10.0, 30.0, 10.0});
    ASSERT_EQ(v.size(), 4u);
    EXPECT_DOUBLE_EQ(v[0], 1.0);              // warmup
    EXPECT_DOUBLE_EQ(v[1], 1.0);              // warmup
    EXPECT_DOUBLE_EQ(v[2], 3.0);              // 30 / moyenne(10,10)
    EXPECT_DOUBLE_EQ(v[3], 10.0 / 20.0);      // 10 / moyenne(10,30)
}

TEST(DayIndicatorsUnit, VolumeOscillatorNeutralOnZeroAverage) {
    VolumeOscillator osc(2);
    auto v = osc.compute({0.0, 0.0, 50.0});
    ASSERT_EQ(v.size(), 3u);
    EXPECT_DOUBLE_EQ(v[2], 1.0);  // moyenne nulle → neutre, pas de division par 0
}
