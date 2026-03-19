// ============================================================
//  test_swing_strategy_integration.cpp  —  Tests INTÉGRATION
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include "strategies/SwingStrategy.hpp"
#include "core/Interfaces.hpp"

using namespace trading;

namespace {

    Bar make_bar(int i, double close, long vol = 100000L) {
        return {"2024-" + std::to_string(i), close, close+0.5, close-0.5, close, vol};
    }

// Marché plat — prix constant sur N barres
    std::vector<Bar> flat(double price, int n = 60) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, price));
        return bars;
    }

// Hausse forte longue → RSI > 70 → SELL garanti
    std::vector<Bar> strong_uptrend(double step = 2.0, int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 100.0 + i * step));
        return bars;
    }

// Baisse forte longue
    std::vector<Bar> strong_downtrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 300.0 - i * 2.0));
        return bars;
    }

// Croisement haussier à la dernière barre :
// stable barres plates puis 1 upbar forte
    std::vector<Bar> bullish_cross_at_last(int stable = 40) {
        std::vector<Bar> bars;
        for (int i = 0; i < stable; ++i)
            bars.push_back(make_bar(i, 100.0));
        bars.push_back(make_bar(stable, 160.0, 200000L));
        return bars;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  Contrats métier
// ════════════════════════════════════════════════════════════

// RSI neutre (50) sur marché plat → pas de SELL ni de BUY → HOLD
TEST(SwingStrategyIntegration, FlatMarketReturnsHold) {
auto s = SwingStrategy::create();
EXPECT_EQ(s->evaluate(flat(100.0, 60)).type, SignalType::HOLD);
EXPECT_EQ(s->evaluate(flat(472.0, 60)).type, SignalType::HOLD);
EXPECT_EQ(s->evaluate(flat( 50.0, 60)).type, SignalType::HOLD);
}

// Plus de barres plates = toujours HOLD
TEST(SwingStrategyIntegration, FlatMarketHoldIsStable) {
auto s = SwingStrategy::create();
EXPECT_EQ(s->evaluate(flat(100.0,  60)).type, SignalType::HOLD);
EXPECT_EQ(s->evaluate(flat(100.0, 120)).type, SignalType::HOLD);
EXPECT_EQ(s->evaluate(flat(100.0, 200)).type, SignalType::HOLD);
}

// Hausse longue → RSI > 70 → SELL
TEST(SwingStrategyIntegration, StrongUptrendReturnsSell) {
EXPECT_EQ(SwingStrategy::create()->evaluate(strong_uptrend(2.0, 80)).type,
SignalType::SELL);
}

// Croisement haussier + RSI permissif → BUY
TEST(SwingStrategyIntegration, BullishCrossoverReturnsBuy) {
SwingConfig cfg;
cfg.rsiBuyMax  = 101.0;
cfg.rsiSellMin = 101.0;
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bullish_cross_at_last(40)).type,
SignalType::BUY);
}

// Croisement haussier standard → pas de SELL (warmup évite faux signaux)
TEST(SwingStrategyIntegration, BullishCrossoverDoesNotReturnSell) {
SwingConfig cfg;
cfg.rsiSellMin = 101.0; // SELL via RSI impossible
EXPECT_NE(SwingStrategy::create(cfg)->evaluate(bullish_cross_at_last(40)).type,
SignalType::SELL);
}

// Baisse forte → pas de BUY
TEST(SwingStrategyIntegration, StrongDowntrendDoesNotReturnBuy) {
EXPECT_NE(SwingStrategy::create()->evaluate(strong_downtrend(80)).type,
SignalType::BUY);
}

// Déterminisme sur les mêmes données
TEST(SwingStrategyIntegration, EvaluateIsDeterministic) {
auto s    = SwingStrategy::create();
auto bars = bullish_cross_at_last();
auto s1   = s->evaluate(bars);
auto s2   = s->evaluate(bars);
EXPECT_EQ(s1.type,   s2.type);
EXPECT_EQ(s1.price,  s2.price);
EXPECT_EQ(s1.reason, s2.reason);
}

// Signal reflète la dernière bougie
TEST(SwingStrategyIntegration, SignalReflectsLastBar) {
auto bars = strong_uptrend();
auto sig  = SwingStrategy::create()->evaluate(bars);
EXPECT_DOUBLE_EQ(sig.price,     bars.back().close);
EXPECT_EQ       (sig.timestamp, bars.back().date);
}

// rsiSellMin bas → SELL déclenché par RSI
TEST(SwingStrategyIntegration, CustomRsiSellMinTriggersEarlier) {
SwingConfig cfg; cfg.rsiSellMin = 30.0;
std::vector<Bar> bars;
for (int i = 0; i < 60; ++i)
bars.push_back(make_bar(i, 100.0 + i * 1.0));
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bars).type, SignalType::SELL);
}

// Gros dataset sans plantage
TEST(SwingStrategyIntegration, HandlesLargeDataset) {
std::vector<Bar> bars;
for (int i = 0; i < 500; ++i)
bars.push_back(make_bar(i, 100.0 + (i % 20) * 0.1));
EXPECT_NO_THROW(SwingStrategy::create()->evaluate(bars));
}

// Tous les types de signal sont valides
TEST(SwingStrategyIntegration, AllSignalTypesAreValid) {
auto s = SwingStrategy::create();
for (auto bars : {flat(100.0), strong_uptrend(), strong_downtrend()}) {
auto sig = s->evaluate(bars);
EXPECT_TRUE(sig.type == SignalType::BUY  ||
sig.type == SignalType::SELL ||
sig.type == SignalType::HOLD);
EXPECT_FALSE(sig.reason.empty());
}
}