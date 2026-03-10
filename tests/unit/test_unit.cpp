#include <gtest/gtest.h>
#include "indicators/Indicators.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"
#include "bot/Mocks.hpp"

using namespace trading;
using namespace trading::mocks;

// ═════════════════════════════════════════════════════════════════════════════
// TESTS — EMA
// ═════════════════════════════════════════════════════════════════════════════

class EMATest : public ::testing::Test {
protected:
    EMA ema3{3};
    EMA ema5{5};
};

TEST_F(EMATest, ThrowsOnInvalidPeriod) {
    EXPECT_THROW(EMA(0),  std::invalid_argument);
    EXPECT_THROW(EMA(-1), std::invalid_argument);
}

TEST_F(EMATest, ReturnsEmptyIfNotEnoughData) {
    auto result = ema5.compute({1.0, 2.0, 3.0});  // seulement 3 valeurs pour période 5
    EXPECT_TRUE(result.empty());
}

TEST_F(EMATest, FirstValueIsSMA) {
    // EMA(3) seed = SMA des 3 premières valeurs = (10+11+12)/3 = 11
    auto result = ema3.compute({10.0, 11.0, 12.0, 13.0});
    EXPECT_NEAR(result[2], 11.0, 0.001);
}

TEST_F(EMATest, ConvergesOnConstantSeries) {
    std::vector<double> prices(50, 100.0);  // prix constant
    auto result = ema5.compute(prices);
    EXPECT_NEAR(result.back(), 100.0, 0.001);
}

TEST_F(EMATest, ReactsFasterThanSlow) {
    // Prix monte de 100 à 120 — EMA rapide devrait être plus haute
    std::vector<double> prices;
    for (int i = 0; i < 20; i++) prices.push_back(100.0);
    for (int i = 0; i < 10; i++) prices.push_back(120.0);

    EMA fast(3), slow(10);
    auto fastVals = fast.compute(prices);
    auto slowVals = slow.compute(prices);

    EXPECT_GT(fastVals.back(), slowVals.back());
}

TEST_F(EMATest, NameContainsPeriod) {
    EXPECT_EQ(ema3.name(), "EMA(3)");
    EXPECT_EQ(ema5.name(), "EMA(5)");
}

// ═════════════════════════════════════════════════════════════════════════════
// TESTS — RSI
// ═════════════════════════════════════════════════════════════════════════════

class RSITest : public ::testing::Test {
protected:
    RSI rsi14{14};
};

TEST_F(RSITest, ThrowsOnPeriodLessThanTwo) {
    EXPECT_THROW(RSI(1), std::invalid_argument);
    EXPECT_THROW(RSI(0), std::invalid_argument);
}

TEST_F(RSITest, Returns100OnAllGains) {
    // Si chaque jour monte, RSI doit être proche de 100
    std::vector<double> prices;
    for (int i = 0; i < 30; i++) prices.push_back(100.0 + i * 1.0);

    auto result = rsi14.compute(prices);
    EXPECT_GT(result.back(), 90.0);
}

TEST_F(RSITest, Returns0OnAllLosses) {
    // Si chaque jour baisse, RSI doit être proche de 0
    std::vector<double> prices;
    for (int i = 0; i < 30; i++) prices.push_back(200.0 - i * 1.0);

    auto result = rsi14.compute(prices);
    EXPECT_LT(result.back(), 10.0);
}

TEST_F(RSITest, StaysBetween0And100) {
    std::vector<double> prices = {
        420, 415, 430, 425, 440, 435, 450, 445, 430, 420,
        415, 410, 425, 435, 440, 445, 450, 460, 455, 465
    };
    auto result = rsi14.compute(prices);
    for (double v : result) {
        if (v > 0) {  // ignore les valeurs non calculées
            EXPECT_GE(v, 0.0);
            EXPECT_LE(v, 100.0);
        }
    }
}

TEST_F(RSITest, ReturnsEmptyIfNotEnoughData) {
    auto result = rsi14.compute({1.0, 2.0, 3.0});
    EXPECT_TRUE(result.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// TESTS — CrossoverDetector
// ═════════════════════════════════════════════════════════════════════════════

TEST(CrossoverDetectorTest, DetectsBullishCrossover) {
    // fast était en dessous, maintenant au-dessus → BULLISH
    std::vector<double> fast = {9.0, 11.0};
    std::vector<double> slow = {10.0, 10.0};
    EXPECT_EQ(CrossoverDetector::detect(fast, slow), CrossoverDetector::Cross::BULLISH);
}

TEST(CrossoverDetectorTest, DetectsBearishCrossover) {
    std::vector<double> fast = {11.0, 9.0};
    std::vector<double> slow = {10.0, 10.0};
    EXPECT_EQ(CrossoverDetector::detect(fast, slow), CrossoverDetector::Cross::BEARISH);
}

TEST(CrossoverDetectorTest, DetectsNoCrossover) {
    std::vector<double> fast = {11.0, 12.0};
    std::vector<double> slow = {10.0, 10.0};
    EXPECT_EQ(CrossoverDetector::detect(fast, slow), CrossoverDetector::Cross::NONE);
}

TEST(CrossoverDetectorTest, ReturnNoneIfTooFewPoints) {
    std::vector<double> fast = {11.0};
    std::vector<double> slow = {10.0};
    EXPECT_EQ(CrossoverDetector::detect(fast, slow), CrossoverDetector::Cross::NONE);
}

// ═════════════════════════════════════════════════════════════════════════════
// TESTS — RiskManager
// ═════════════════════════════════════════════════════════════════════════════

class RiskManagerTest : public ::testing::Test {
protected:
    RiskManager rm;
};

TEST_F(RiskManagerTest, CalculatesPositionSizeCorrectly) {
    // capital=$10,000, risk=2%, prix=$420, stop=5%
    // dollarRisk = 200, riskPerShare = 21 → 9 shares
    int shares = rm.positionSize(10000.0, 420.0, 0.05, 0.02);
    EXPECT_EQ(shares, 9);
}

TEST_F(RiskManagerTest, NeverBelowOneShare) {
    // capital minimal, prix élevé
    int shares = rm.positionSize(100.0, 5000.0, 0.05, 0.02);
    EXPECT_GE(shares, 1);
}

TEST_F(RiskManagerTest, ReturnsZeroOnInvalidInputs) {
    EXPECT_EQ(rm.positionSize(0, 420, 0.05, 0.02), 0);
    EXPECT_EQ(rm.positionSize(10000, 0, 0.05, 0.02), 0);
    EXPECT_EQ(rm.positionSize(10000, 420, 0, 0.02), 0);
}

TEST_F(RiskManagerTest, AllowsTradeWhenConditionsMet) {
    Account acct = {10000.0, 10000.0, "ACTIVE"};
    EXPECT_TRUE(rm.isTradeAllowed(acct, std::nullopt));
}

TEST_F(RiskManagerTest, BlocksTradeIfAlreadyInPosition) {
    Account acct = {5000.0, 10000.0, "ACTIVE"};
    Position pos; pos.shares = 10;
    EXPECT_FALSE(rm.isTradeAllowed(acct, pos));
}

TEST_F(RiskManagerTest, BlocksTradeIfInactiveAccount) {
    Account acct = {10000.0, 10000.0, "SUSPENDED"};
    EXPECT_FALSE(rm.isTradeAllowed(acct, std::nullopt));
}

TEST_F(RiskManagerTest, TriggersStopLoss) {
    // achat à $420, prix actuel $399 → perte de 5%
    auto reason = rm.checkExitConditions(399.0, 420.0, 5, 425.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(reason.has_value());
    EXPECT_NE(reason->find("stop-loss"), std::string::npos);
}

TEST_F(RiskManagerTest, TriggersTakeProfit) {
    // achat à $420, prix actuel $462 → gain de 10%
    auto reason = rm.checkExitConditions(462.0, 420.0, 5, 462.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(reason.has_value());
    EXPECT_NE(reason->find("take-profit"), std::string::npos);
}

TEST_F(RiskManagerTest, TriggersTrailingStop) {
    // pic à $460, prix actuel $446 → baisse de 3% depuis le pic
    auto reason = rm.checkExitConditions(446.0, 420.0, 5, 460.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(reason.has_value());
    EXPECT_NE(reason->find("trailing"), std::string::npos);
}

TEST_F(RiskManagerTest, TrailingStopNotTriggeredBeforeMinDays) {
    // même situation mais holdDays=2 < minHoldDays=3
    auto reason = rm.checkExitConditions(446.0, 420.0, 2, 460.0, 0.05, 0.10, 0.03, 3);
    EXPECT_FALSE(reason.has_value());
}

TEST_F(RiskManagerTest, NoExitWhenProfitable) {
    // position gagnante, pas de condition de sortie
    auto reason = rm.checkExitConditions(435.0, 420.0, 2, 440.0, 0.05, 0.10, 0.03, 3);
    EXPECT_FALSE(reason.has_value());
}

// ═════════════════════════════════════════════════════════════════════════════
// TESTS — SwingStrategy
// ═════════════════════════════════════════════════════════════════════════════

class SwingStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<SwingStrategy> strategy = SwingStrategy::create();
};

TEST_F(SwingStrategyTest, ReturnsHoldWhenNotEnoughBars) {
    std::vector<Bar> bars(5);  // pas assez de barres
    for (auto& b : bars) b.close = 420.0;
    auto signal = strategy->evaluate(bars);
    EXPECT_EQ(signal.type, SignalType::HOLD);
}

TEST_F(SwingStrategyTest, ReturnsHoldOnFlatMarket) {
    // Prix constant → pas de croisement → HOLD
    auto bars = MockDataFeed::buildTrend(420.0, 40, 0.0);
    auto signal = strategy->evaluate(bars);
    EXPECT_EQ(signal.type, SignalType::HOLD);
}

TEST_F(SwingStrategyTest, HasCorrectSymbol) {
    auto bars = MockDataFeed::buildTrend(420.0, 40, 0.1);
    auto signal = strategy->evaluate(bars);
    EXPECT_EQ(signal.symbol, "QQQ");
}

TEST_F(SwingStrategyTest, ReturnsNonZeroPrice) {
    auto bars = MockDataFeed::buildTrend(420.0, 40, 0.1);
    auto signal = strategy->evaluate(bars);
    EXPECT_GT(signal.price, 0.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
