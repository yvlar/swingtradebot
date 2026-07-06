// ============================================================
//  test_indicators_unit.cpp  —  Tests UNITAIRES
//  Cible : Indicators.hpp — EMA, RSI, CrossoverDetector
//  Le cœur mathématique de la stratégie : valeurs vérifiées
//  contre des calculs à la main (pas seulement des propriétés)
// ============================================================
#include <gtest/gtest.h>
#include "indicators/Indicators.hpp"

using namespace trading;
using Cross = CrossoverDetector::Cross;

// ════════════════════════════════════════════════════════════
//  EMA — seed SMA puis récurrence k = 2/(period+1)
// ════════════════════════════════════════════════════════════

TEST(IndicatorsUnit, EmaThrowsOnInvalidPeriod) {
    EXPECT_THROW(EMA(0),  std::invalid_argument);
    EXPECT_THROW(EMA(-3), std::invalid_argument);
}

TEST(IndicatorsUnit, EmaNameAndPeriod) {
    EMA ema(12);
    EXPECT_EQ(ema.name(), "EMA(12)");
    EXPECT_EQ(ema.period(), 12);
}

TEST(IndicatorsUnit, EmaInsufficientDataReturnsEmpty) {
    EMA ema(5);
    EXPECT_TRUE(ema.compute({}).empty());
    EXPECT_TRUE(ema.compute({1.0, 2.0, 3.0, 4.0}).empty());
}

// Calcul à la main : period 3 → k = 0,5
//   seed (idx 2) = SMA(1,2,3) = 2
//   idx 3 = 4×0,5 + 2×0,5 = 3 ;  idx 4 = 5×0,5 + 3×0,5 = 4
TEST(IndicatorsUnit, EmaMatchesHandComputedValues) {
    EMA ema(3);
    auto r = ema.compute({1.0, 2.0, 3.0, 4.0, 5.0});

    ASSERT_EQ(r.size(), 5u);
    EXPECT_DOUBLE_EQ(r[0], 0.0);   // avant le seed : non défini → 0
    EXPECT_DOUBLE_EQ(r[1], 0.0);
    EXPECT_DOUBLE_EQ(r[2], 2.0);   // seed SMA
    EXPECT_DOUBLE_EQ(r[3], 3.0);
    EXPECT_DOUBLE_EQ(r[4], 4.0);
}

// Exactement `period` valeurs : seul le seed SMA est défini
TEST(IndicatorsUnit, EmaExactPeriodReturnsSeedOnly) {
    EMA ema(3);
    auto r = ema.compute({3.0, 6.0, 9.0});

    ASSERT_EQ(r.size(), 3u);
    EXPECT_DOUBLE_EQ(r[2], 6.0);   // SMA(3,6,9)
}

// Prix constant → l'EMA reste collée au prix (pas de dérive numérique)
TEST(IndicatorsUnit, EmaConstantPricesStaysFlat) {
    EMA ema(4);
    auto r = ema.compute(std::vector<double>(50, 420.0));

    ASSERT_EQ(r.size(), 50u);
    for (size_t i = 3; i < r.size(); ++i)
        EXPECT_DOUBLE_EQ(r[i], 420.0) << "à l'index " << i;
}

// ════════════════════════════════════════════════════════════
//  SMA — moyenne mobile simple (filtre de régime, item 8.1)
// ════════════════════════════════════════════════════════════

TEST(IndicatorsUnit, SmaThrowsOnInvalidPeriod) {
    EXPECT_THROW(SMA(0),  std::invalid_argument);
    EXPECT_THROW(SMA(-4), std::invalid_argument);
}

TEST(IndicatorsUnit, SmaNameAndPeriod) {
    SMA sma(200);
    EXPECT_EQ(sma.name(), "SMA(200)");
    EXPECT_EQ(sma.period(), 200);
}

TEST(IndicatorsUnit, SmaInsufficientDataReturnsEmpty) {
    SMA sma(5);
    EXPECT_TRUE(sma.compute({}).empty());
    EXPECT_TRUE(sma.compute({1.0, 2.0, 3.0, 4.0}).empty());
}

// Calcul à la main : period 3 sur {1,2,3,4,5}
//   seed (idx 2) = (1+2+3)/3 = 2 ; idx 3 = (2+3+4)/3 = 3 ; idx 4 = (3+4+5)/3 = 4
TEST(IndicatorsUnit, SmaMatchesHandComputedValues) {
    SMA sma(3);
    auto r = sma.compute({1.0, 2.0, 3.0, 4.0, 5.0});

    ASSERT_EQ(r.size(), 5u);
    EXPECT_DOUBLE_EQ(r[0], 0.0);   // avant le seed : non défini → 0
    EXPECT_DOUBLE_EQ(r[1], 0.0);
    EXPECT_DOUBLE_EQ(r[2], 2.0);
    EXPECT_DOUBLE_EQ(r[3], 3.0);
    EXPECT_DOUBLE_EQ(r[4], 4.0);
}

// Exactement `period` valeurs : seul le seed est défini
TEST(IndicatorsUnit, SmaExactPeriodReturnsSeedOnly) {
    SMA sma(4);
    auto r = sma.compute({2.0, 4.0, 6.0, 8.0});
    ASSERT_EQ(r.size(), 4u);
    EXPECT_DOUBLE_EQ(r[3], 5.0);   // (2+4+6+8)/4
}

// Prix constant → SMA collée au prix, pas de dérive de la somme glissante
TEST(IndicatorsUnit, SmaConstantPricesStaysFlat) {
    SMA sma(10);
    auto r = sma.compute(std::vector<double>(60, 350.0));
    ASSERT_EQ(r.size(), 60u);
    for (size_t i = 9; i < r.size(); ++i)
        EXPECT_DOUBLE_EQ(r[i], 350.0) << "à l'index " << i;
}

// ════════════════════════════════════════════════════════════
//  RSI — moyenne lissée de Wilder
// ════════════════════════════════════════════════════════════

TEST(IndicatorsUnit, RsiThrowsOnInvalidPeriod) {
    EXPECT_THROW(RSI(1),  std::invalid_argument);
    EXPECT_THROW(RSI(0),  std::invalid_argument);
    EXPECT_THROW(RSI(-5), std::invalid_argument);
}

TEST(IndicatorsUnit, RsiNameAndPeriod) {
    RSI rsi(14);
    EXPECT_EQ(rsi.name(), "RSI(14)");
    EXPECT_EQ(rsi.period(), 14);
}

// Il faut period+1 prix (period variations) pour une première valeur
TEST(IndicatorsUnit, RsiInsufficientDataReturnsEmpty) {
    RSI rsi(14);
    EXPECT_TRUE(rsi.compute({}).empty());
    EXPECT_TRUE(rsi.compute(std::vector<double>(14, 100.0)).empty());
}

// Calcul à la main : period 2, prix {10, 11, 12, 11}
//   gains {1,1,0}, pertes {0,0,1}
//   idx 2 : avgGain 1, avgLoss 0 → RSI 100 (que des hausses)
//   idx 3 : avgGain (1×1+0)/2 = 0,5 ; avgLoss (0×1+1)/2 = 0,5 → RS 1 → RSI 50
TEST(IndicatorsUnit, RsiMatchesHandComputedWilderValues) {
    RSI rsi(2);
    auto r = rsi.compute({10.0, 11.0, 12.0, 11.0});

    ASSERT_EQ(r.size(), 4u);
    EXPECT_DOUBLE_EQ(r[0], 0.0);   // avant la première valeur : 0
    EXPECT_DOUBLE_EQ(r[1], 0.0);
    EXPECT_DOUBLE_EQ(r[2], 100.0);
    EXPECT_DOUBLE_EQ(r[3], 50.0);
}

// Marché plat : aucun gain ni perte → RSI neutre (50), pas de division 0/0
TEST(IndicatorsUnit, RsiFlatMarketIsNeutral50) {
    RSI rsi(14);
    auto r = rsi.compute(std::vector<double>(40, 350.0));

    ASSERT_EQ(r.size(), 40u);
    for (size_t i = 14; i < r.size(); ++i)
        EXPECT_DOUBLE_EQ(r[i], 50.0) << "à l'index " << i;
}

// Hausse monotone : aucune perte → RSI saturé à 100
TEST(IndicatorsUnit, RsiMonotonicUptrendSaturatesAt100) {
    RSI rsi(14);
    std::vector<double> prices;
    for (int i = 0; i < 40; ++i) prices.push_back(100.0 + i);

    auto r = rsi.compute(prices);
    ASSERT_EQ(r.size(), 40u);
    EXPECT_DOUBLE_EQ(r.back(), 100.0);
}

// Baisse monotone : aucun gain → RSI à 0
TEST(IndicatorsUnit, RsiMonotonicDowntrendReaches0) {
    RSI rsi(14);
    std::vector<double> prices;
    for (int i = 0; i < 40; ++i) prices.push_back(200.0 - i);

    auto r = rsi.compute(prices);
    ASSERT_EQ(r.size(), 40u);
    EXPECT_DOUBLE_EQ(r.back(), 0.0);
}

// Propriété : le RSI est toujours borné [0, 100], même sur série agitée
TEST(IndicatorsUnit, RsiAlwaysWithinBounds) {
    RSI rsi(14);
    std::vector<double> prices;
    for (int i = 0; i < 200; ++i)
        prices.push_back(300.0 + ((i * 37) % 23) - ((i * 13) % 11));

    auto r = rsi.compute(prices);
    ASSERT_EQ(r.size(), prices.size());
    for (size_t i = 14; i < r.size(); ++i) {
        EXPECT_GE(r[i], 0.0)   << "à l'index " << i;
        EXPECT_LE(r[i], 100.0) << "à l'index " << i;
    }
}

// ════════════════════════════════════════════════════════════
//  CrossoverDetector — croisements au dernier index
// ════════════════════════════════════════════════════════════

TEST(IndicatorsUnit, CrossoverDetectsBullish) {
    // fast passe sous → au-dessus de slow
    EXPECT_EQ(CrossoverDetector::detect({1.0, 3.0}, {2.0, 2.0}), Cross::BULLISH);
}

TEST(IndicatorsUnit, CrossoverDetectsBearish) {
    EXPECT_EQ(CrossoverDetector::detect({3.0, 1.0}, {2.0, 2.0}), Cross::BEARISH);
}

TEST(IndicatorsUnit, CrossoverNoneWhenNoCross) {
    // fast reste au-dessus
    EXPECT_EQ(CrossoverDetector::detect({3.0, 4.0}, {2.0, 2.0}), Cross::NONE);
    // fast reste en dessous
    EXPECT_EQ(CrossoverDetector::detect({1.0, 1.5}, {2.0, 2.0}), Cross::NONE);
}

// Contrat figé : « toucher » (égalité) ne compte pas comme être au-dessus —
// l'égalité précédente suivie d'un dépassement est donc un croisement
TEST(IndicatorsUnit, CrossoverEqualityThenAboveIsBullish) {
    EXPECT_EQ(CrossoverDetector::detect({2.0, 3.0}, {2.0, 2.0}), Cross::BULLISH);
}

TEST(IndicatorsUnit, CrossoverNoneOnTooShortSeries) {
    EXPECT_EQ(CrossoverDetector::detect({}, {}),         Cross::NONE);
    EXPECT_EQ(CrossoverDetector::detect({1.0}, {2.0}),   Cross::NONE);
}

// Frontière du warmup (n-1 <= warmup) : un croisement au dernier index
// est ignoré tant que cet index est dans la période de warmup
TEST(IndicatorsUnit, CrossoverWarmupSuppressesEarlySignal) {
    std::vector<double> fast = {1.0, 1.0, 3.0};
    std::vector<double> slow = {2.0, 2.0, 2.0};

    // dernier index = 2 : supprimé si warmup >= 2, détecté si warmup = 1
    EXPECT_EQ(CrossoverDetector::detect(fast, slow, 2), Cross::NONE);
    EXPECT_EQ(CrossoverDetector::detect(fast, slow, 3), Cross::NONE);
    EXPECT_EQ(CrossoverDetector::detect(fast, slow, 1), Cross::BULLISH);
    EXPECT_EQ(CrossoverDetector::detect(fast, slow, 0), Cross::BULLISH);
}

// ════════════════════════════════════════════════════════════
//  IIndicator::computeBars — défaut rétro-compatible (item 8.0)
// ════════════════════════════════════════════════════════════

namespace {
trading::Bar barClose(double close) {
    trading::Bar b;
    b.close = close;
    return b;
}
} // namespace

// Le défaut de computeBars extrait les clôtures : pour des indicateurs qui ne
// dépendent que de la clôture (EMA/RSI), computeBars(bars) ≡ compute(closes).
TEST(IndicatorsUnit, ComputeBarsDefaultMatchesComputeOnClosesEma) {
    EMA ema(3);
    std::vector<double> closes = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<Bar> bars;
    for (double c : closes) bars.push_back(barClose(c));

    EXPECT_EQ(ema.computeBars(bars), ema.compute(closes));
}

TEST(IndicatorsUnit, ComputeBarsDefaultMatchesComputeOnClosesRsi) {
    RSI rsi(2);
    std::vector<double> closes = {10.0, 11.0, 12.0, 11.0};
    std::vector<Bar> bars;
    for (double c : closes) bars.push_back(barClose(c));

    EXPECT_EQ(rsi.computeBars(bars), rsi.compute(closes));
}

// ════════════════════════════════════════════════════════════
//  RollingStdDev — écart-type de population glissant (Sprint 11)
//  Brique du z-score de la variante mean-reversion. Valeurs
//  vérifiées à la main (σ = √(E[x²] − E[x]²)).
// ════════════════════════════════════════════════════════════

TEST(IndicatorsUnit, RollingStdDevThrowsOnInvalidPeriod) {
    EXPECT_THROW(RollingStdDev(0),  std::invalid_argument);
    EXPECT_THROW(RollingStdDev(-2), std::invalid_argument);
}

// Série plus courte que la période → vide (convention SMA).
TEST(IndicatorsUnit, RollingStdDevTooShortReturnsEmpty) {
    RollingStdDev sd(5);
    std::vector<double> prices = {1.0, 2.0, 3.0};
    EXPECT_TRUE(sd.compute(prices).empty());
}

// Cas d'école : σ de population de {2,4,4,4,5,5,7,9} = 2.0 exactement
// (moyenne 5, variance 32/8 = 4). result[period-1] porte la valeur.
TEST(IndicatorsUnit, RollingStdDevTextbookPopulationValue) {
    RollingStdDev sd(8);
    std::vector<double> prices = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    auto r = sd.compute(prices);
    ASSERT_EQ(r.size(), prices.size());
    EXPECT_EQ(r[0], 0.0);          // warmup
    EXPECT_NEAR(r[7], 2.0, 1e-9);
}

// Série plate → écart-type nul partout (aucun bruit d'arrondi négatif).
TEST(IndicatorsUnit, RollingStdDevFlatSeriesIsZero) {
    RollingStdDev sd(4);
    std::vector<double> prices = {7.0, 7.0, 7.0, 7.0, 7.0, 7.0};
    auto r = sd.compute(prices);
    ASSERT_EQ(r.size(), prices.size());
    for (size_t i = 3; i < r.size(); ++i) EXPECT_NEAR(r[i], 0.0, 1e-12);
}

// Fenêtre glissante : trois fenêtres calculées à la main.
//   [1,2,3] σ=√(2/3)=0.8164966 ; [2,3,4] σ=√(2/3) ; [3,4,6] σ=√(14/9)=1.2472191.
TEST(IndicatorsUnit, RollingStdDevSlidingWindowHandComputed) {
    RollingStdDev sd(3);
    std::vector<double> prices = {1.0, 2.0, 3.0, 4.0, 6.0};
    auto r = sd.compute(prices);
    ASSERT_EQ(r.size(), 5u);
    EXPECT_EQ(r[0], 0.0);
    EXPECT_EQ(r[1], 0.0);
    EXPECT_NEAR(r[2], 0.81649658, 1e-7);
    EXPECT_NEAR(r[3], 0.81649658, 1e-7);
    EXPECT_NEAR(r[4], 1.24721913, 1e-7);
}
