// ============================================================
//  test_swing_strategy_unit.cpp  —  Tests UNITAIRES
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include "strategies/SwingStrategy.hpp"
#include "core/Interfaces.hpp"

using namespace trading;

namespace {

    Bar make_bar(int i, double close, long vol = 100000L) {
        return {"2024-" + std::to_string(i), close, close+0.5, close-0.5, close, vol};
    }

// Marché plat : prix strictement constant
    std::vector<Bar> flat(double price, int n = 60) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, price));
        return bars;
    }

// Hausse soutenue longue → RSI > 70 → SELL garanti
    std::vector<Bar> strong_uptrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 100.0 + i * 2.0));
        return bars;
    }

// Baisse soutenue longue
    std::vector<Bar> strong_downtrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 300.0 - i * 2.0));
        return bars;
    }

// Croisement haussier à la dernière barre :
// N-1 barres plates puis 1 seule upbar → EMAfast croise EMAslow exactement à la fin
    std::vector<Bar> bullish_cross_at_last(int stable = 40) {
        std::vector<Bar> bars;
        for (int i = 0; i < stable; ++i)
            bars.push_back(make_bar(i, 100.0));
        // Une seule upbar forte → croisement garanti à la dernière barre
        bars.push_back(make_bar(stable, 160.0, 200000L));
        return bars;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  Création & configuration
// ════════════════════════════════════════════════════════════

TEST(SwingStrategyUnit, CreateSucceeds) {
EXPECT_NE(SwingStrategy::create(), nullptr);
}

TEST(SwingStrategyUnit, NameIsCorrect) {
EXPECT_EQ(SwingStrategy::create()->name(), "SwingStrategy_EMA_RSI");
}

TEST(SwingStrategyUnit, DefaultConfigValues) {
SwingConfig cfg;
EXPECT_EQ(cfg.emaFast,    9);
EXPECT_EQ(cfg.emaSlow,   21);
EXPECT_EQ(cfg.rsiPeriod, 14);
EXPECT_DOUBLE_EQ(cfg.rsiBuyMax,       55.0);
EXPECT_DOUBLE_EQ(cfg.rsiSellMin,      70.0);
EXPECT_DOUBLE_EQ(cfg.stopLossPct,     0.05);
EXPECT_DOUBLE_EQ(cfg.takeProfitPct,   0.10);
EXPECT_DOUBLE_EQ(cfg.trailingStopPct, 0.03);
EXPECT_DOUBLE_EQ(cfg.riskPerTradePct, 0.02);
EXPECT_EQ(cfg.minHoldDays, 3);
}

TEST(SwingStrategyUnit, CustomConfigIsStored) {
SwingConfig cfg;
cfg.symbol  = "SPY";
cfg.emaFast = 5;
cfg.emaSlow = 15;
auto s = SwingStrategy::create(cfg);
EXPECT_EQ(s->config().symbol,  "SPY");
EXPECT_EQ(s->config().emaFast,   5);
EXPECT_EQ(s->config().emaSlow,  15);
}

// ════════════════════════════════════════════════════════════
//  Cas limites
// ════════════════════════════════════════════════════════════

TEST(SwingStrategyUnit, EmptyBarsReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate({}).type, SignalType::HOLD);
}

TEST(SwingStrategyUnit, InsufficientBarsReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate(flat(100.0, 10)).type, SignalType::HOLD);
}

TEST(SwingStrategyUnit, InsufficientBarsReasonMentionsDonnees) {
auto sig = SwingStrategy::create()->evaluate({});
EXPECT_NE(sig.reason.find("données"), std::string::npos);
}

TEST(SwingStrategyUnit, SignalTypeAlwaysValid) {
auto s = SwingStrategy::create();
for (auto& bars : {flat(100.0), strong_uptrend(), strong_downtrend()}) {
auto t = s->evaluate(bars).type;
EXPECT_TRUE(t == SignalType::BUY  ||
t == SignalType::SELL ||
t == SignalType::HOLD);
}
}

TEST(SwingStrategyUnit, SignalPriceIsLastClose) {
auto bars = strong_uptrend();
EXPECT_DOUBLE_EQ(SwingStrategy::create()->evaluate(bars).price,
bars.back().close);
}

TEST(SwingStrategyUnit, SignalTimestampIsLastBar) {
auto bars = strong_uptrend();
EXPECT_EQ(SwingStrategy::create()->evaluate(bars).timestamp,
bars.back().date);
}

TEST(SwingStrategyUnit, SignalSymbolMatchesConfig) {
SwingConfig cfg; cfg.symbol = "SPY";
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(flat(100.0)).symbol, "SPY");
}

TEST(SwingStrategyUnit, SignalReasonIsNotEmpty) {
EXPECT_FALSE(SwingStrategy::create()->evaluate(strong_uptrend()).reason.empty());
}

TEST(SwingStrategyUnit, EvaluateIsDeterministic) {
auto s    = SwingStrategy::create();
auto bars = strong_uptrend();
auto s1   = s->evaluate(bars);
auto s2   = s->evaluate(bars);
EXPECT_EQ(s1.type,   s2.type);
EXPECT_EQ(s1.price,  s2.price);
EXPECT_EQ(s1.reason, s2.reason);
}

// ════════════════════════════════════════════════════════════
//  Logique métier
// ════════════════════════════════════════════════════════════

// RSI neutre sur marché plat (fix: toRSI(0,0) = 50, pas 100)
TEST(SwingStrategyUnit, FlatMarketReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate(flat(100.0, 60)).type,
SignalType::HOLD);
}

// Hausse forte → RSI > rsiSellMin(70) → SELL
TEST(SwingStrategyUnit, StrongUptrendReturnsSell) {
EXPECT_EQ(SwingStrategy::create()->evaluate(strong_uptrend(80)).type,
SignalType::SELL);
}

// Baisse forte → pas de BUY
TEST(SwingStrategyUnit, StrongDowntrendDoesNotReturnBuy) {
EXPECT_NE(SwingStrategy::create()->evaluate(strong_downtrend(80)).type,
SignalType::BUY);
}

// Croisement haussier à la dernière barre + RSI permissif → BUY
TEST(SwingStrategyUnit, BullishCrossoverReturnsBuyWhenRsiPermissive) {
SwingConfig cfg;
cfg.rsiBuyMax  = 101.0;  // RSI toujours acceptable
cfg.rsiSellMin = 101.0;  // jamais de SELL via RSI
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bullish_cross_at_last(40)).type,
SignalType::BUY);
}

// rsiSellMin très bas → SELL rapide
TEST(SwingStrategyUnit, LowRsiSellMinTriggersSellEarly) {
SwingConfig cfg; cfg.rsiSellMin = 30.0;
std::vector<Bar> bars;
for (int i = 0; i < 60; ++i)
bars.push_back(make_bar(i, 100.0 + i * 1.0));
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bars).type, SignalType::SELL);
}

// rsiBuyMax très bas → BUY impossible
TEST(SwingStrategyUnit, VeryLowRsiBuyMaxBlocksBuy) {
SwingConfig cfg; cfg.rsiBuyMax = 1.0;
EXPECT_NE(SwingStrategy::create(cfg)->evaluate(strong_uptrend(80)).type,
SignalType::BUY);
}

TEST(SwingStrategyUnit, LargeDatasetNoThrow) {
auto s = SwingStrategy::create();
std::vector<Bar> bars;
for (int i = 0; i < 500; ++i)
bars.push_back(make_bar(i, 100.0 + i * 0.1));
EXPECT_NO_THROW(s->evaluate(bars));
}
// ════════════════════════════════════════════════════════════
//  Compléments de couverture — conversion RiskConfig,
//  garde « indicateurs vides »
// ════════════════════════════════════════════════════════════

// Item 12 : les composition roots passent une SwingConfig au bot via cette
// conversion — le bot ne dépend jamais de la stratégie
TEST(SwingStrategyUnit, SwingConfigConvertsToRiskConfig) {
    SwingConfig cfg;
    cfg.symbol          = "SPY";
    cfg.stopLossPct     = 0.04;
    cfg.takeProfitPct   = 0.12;
    cfg.trailingStopPct = 0.02;
    cfg.riskPerTradePct = 0.01;
    cfg.minHoldDays     = 5;

    RiskConfig r = cfg;
    EXPECT_EQ(r.symbol, "SPY");
    EXPECT_DOUBLE_EQ(r.stopLossPct,     0.04);
    EXPECT_DOUBLE_EQ(r.takeProfitPct,   0.12);
    EXPECT_DOUBLE_EQ(r.trailingStopPct, 0.02);
    EXPECT_DOUBLE_EQ(r.riskPerTradePct, 0.01);
    EXPECT_EQ(r.minHoldDays, 5);
}

namespace {

// Indicateur qui ne produit jamais rien — simule un échec de calcul
class EmptyIndicator final : public IIndicator<double> {
public:
    std::vector<double> compute(const std::vector<double>&) const override { return {}; }
    std::string name() const override { return "Empty"; }
};

} // namespace

// Indicateurs injectés défaillants : HOLD explicite, jamais de crash
TEST(SwingStrategyUnit, FailedIndicatorComputationReturnsHold) {
    SwingStrategy strat(SwingConfig{},
                        std::make_unique<EmptyIndicator>(),
                        std::make_unique<EmptyIndicator>(),
                        std::make_unique<EmptyIndicator>());

    auto sig = strat.evaluate(flat(400.0, 60));
    EXPECT_TRUE(sig.isHold());
    EXPECT_NE(sig.reason.find("indicateurs"), std::string::npos);
}
