// ============================================================
//  test_day_trade_strategy_unit.cpp  —  Tests UNITAIRES
//  Cible : DayTradeStrategy (strategies/DayTradeStrategy.hpp)
//  Item 13 : ce TU garantit que la stratégie COMPILE — elle incluait
//  un header inexistant et ne cassait jamais le build car aucun
//  fichier ne l'incluait.
// ============================================================
#include <gtest/gtest.h>
#include <memory>
#include "strategies/DayTradeStrategy.hpp"

using namespace trading;

namespace {

// Série de barres intraday (même session) à prix et volume paramétrables
std::vector<Bar> makeBars(int count, double startPrice, double stepPerBar,
                          long volume = 1'000) {
    std::vector<Bar> bars;
    bars.reserve(count);
    for (int i = 0; i < count; ++i) {
        Bar b;
        char ts[32];
        std::snprintf(ts, sizeof(ts), "2024-03-05 %02d:%02d",
                      9 + (i * 5) / 60, (30 + i * 5) % 60);
        b.date   = ts;
        double p = startPrice + stepPerBar * i;
        b.open = b.high = b.low = b.close = p;
        b.volume = volume;
        bars.push_back(b);
    }
    return bars;
}

} // namespace

TEST(DayTradeStrategyUnit, NotEnoughBarsReturnsHold) {
    auto strat = DayTradeStrategy::create();
    auto sig = strat->evaluate(makeBars(10, 100.0, 0.0));
    EXPECT_TRUE(sig.isHold());
    EXPECT_EQ(sig.reason, "Pas assez de barres");
}

TEST(DayTradeStrategyUnit, FlatMarketHolds) {
    // Marché plat : pas de croisement, RSI neutre (50), prix = VWAP → HOLD
    auto strat = DayTradeStrategy::create();
    auto sig = strat->evaluate(makeBars(60, 100.0, 0.0));
    EXPECT_TRUE(sig.isHold());
}

TEST(DayTradeStrategyUnit, PriceBelowVwapTriggersSell) {
    // Prix en baisse régulière : le dernier prix est sous le VWAP cumulé
    // de la session → signal de VENTE
    auto strat = DayTradeStrategy::create();
    auto sig = strat->evaluate(makeBars(60, 100.0, -0.2));
    EXPECT_TRUE(sig.isSell());
}

TEST(DayTradeStrategyUnit, ComputeAtrStopScalesWithVolatility) {
    auto strat = DayTradeStrategy::create();
    // Marché plat : ATR=0 → stop ATR nul ; marché agité → stop positif
    auto flat = makeBars(60, 100.0, 0.0);
    EXPECT_DOUBLE_EQ(strat->computeATRStop(flat), 0.0);

    auto moving = makeBars(60, 100.0, -0.5);
    EXPECT_GT(strat->computeATRStop(moving), 0.0);
}
