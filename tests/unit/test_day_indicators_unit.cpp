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

// ── ATR computeBars : vrai true range (item 8.0) ────────────────────────────

namespace {
// Barre OHLC explicite (range intraday distinct du mouvement de clôture)
Bar makeOhlc(double open, double high, double low, double close) {
    Bar b;
    b.date = "2024-03-05";
    b.open = open; b.high = high; b.low = low; b.close = close;
    b.volume = 1'000;
    return b;
}
} // namespace

TEST(DayIndicatorsUnit, AtrComputeBarsTooFewPointsReturnsEmpty) {
    ATR atr(14);
    std::vector<Bar> bars(14, makeOhlc(100, 101, 99, 100));  // il faut period+1
    EXPECT_TRUE(atr.computeBars(bars).empty());
}

// Calcul à la main, period=2 :
//   barres (H,L,Cprev) → TR = max(H−L, |H−Cprev|, |L−Cprev|)
//   b0 close=10 (pas de TR, pas de clôture précédente)
//   b1 H=14 L=11 Cprev=10 → max(3, 4, 1) = 4
//   b2 H=12 L=9  Cprev=13 → max(3, 1, 4) = 4
//   b3 H=20 L=15 Cprev=10 → max(5,10, 5) = 10
//   seed (indice 2) = SMA(TR1,TR2) = (4+4)/2 = 4
//   indice 3 = (4·1 + 10)/2 = 7
TEST(DayIndicatorsUnit, AtrComputeBarsUsesTrueRange) {
    ATR atr(2);
    std::vector<Bar> bars{
        makeOhlc(10, 10, 10, 10),
        makeOhlc(12, 14, 11, 13),
        makeOhlc(13, 12,  9, 10),
        makeOhlc(11, 20, 15, 18),
    };
    auto v = atr.computeBars(bars);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_DOUBLE_EQ(v[0], 0.0);   // warmup
    EXPECT_DOUBLE_EQ(v[1], 0.0);
    EXPECT_DOUBLE_EQ(v[2], 4.0);   // seed
    EXPECT_DOUBLE_EQ(v[3], 7.0);
}

// Le vrai true range dépasse l'approximation clôture-à-clôture dès qu'il y a un
// range intraday : ici les clôtures sont constantes (|Δclôture|=0) mais chaque
// barre a une amplitude H−L de 4 → ATR true-range = 4, ATR clôture-seule = 0.
TEST(DayIndicatorsUnit, AtrComputeBarsExceedsCloseToCloseWhenIntradayRange) {
    ATR atr(2);
    std::vector<Bar> bars{
        makeOhlc(100, 102, 98, 100),
        makeOhlc(100, 102, 98, 100),
        makeOhlc(100, 102, 98, 100),
        makeOhlc(100, 102, 98, 100),
    };
    auto trueRange = atr.computeBars(bars);
    std::vector<double> closes{100, 100, 100, 100};
    auto closeOnly = atr.compute(closes);

    ASSERT_EQ(trueRange.size(), 4u);
    EXPECT_DOUBLE_EQ(trueRange[3], 4.0);   // amplitude H−L
    EXPECT_DOUBLE_EQ(closeOnly[3], 0.0);   // dégradé : aucun mouvement de clôture
    EXPECT_GT(trueRange[3], closeOnly[3]);
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

// Via l'interface polymorphe, computeBars donne le VWAP pondéré volume
// (≡ computeWithVolume), pas la moyenne cumulative dégradée de compute (item 8.0)
TEST(DayIndicatorsUnit, VwapComputeBarsEqualsComputeWithVolume) {
    VWAP vwap;
    std::vector<Bar> bars{
        makeBar("2024-03-05 09:30", 10.0, 1),
        makeBar("2024-03-05 09:35", 20.0, 3),
    };
    EXPECT_EQ(vwap.computeBars(bars), vwap.computeWithVolume(bars));
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

// ════════════════════════════════════════════════════════════
//  name() — identifiants pour le logging
// ════════════════════════════════════════════════════════════

TEST(DayIndicatorsUnit, IndicatorNamesCarryPeriod) {
    EXPECT_EQ(ATR(14).name(),               "ATR(14)");
    EXPECT_EQ(VWAP().name(),                "VWAP");
    EXPECT_EQ(VolumeOscillator(20).name(),  "VolumeOscillator(20)");
}
