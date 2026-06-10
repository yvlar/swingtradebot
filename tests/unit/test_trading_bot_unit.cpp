// ============================================================
//  test_trading_bot_unit.cpp  —  Tests UNITAIRES
//  Cible : TradingBot::runOnce — cycle de trading complet sur mocks
// ============================================================
#include <gtest/gtest.h>
#include <memory>
#include "bot/TradingBot.hpp"
#include "bot/RiskManager.hpp"
#include "bot/Logger.hpp"
#include "bot/Mocks.hpp"

using namespace trading;
using namespace trading::mocks;

namespace {

// ── Harnais : TradingBot câblé sur mocks, accès direct aux collaborateurs ──
struct BotHarness {
    std::shared_ptr<MockDataFeed>  feed     = std::make_shared<MockDataFeed>();
    std::shared_ptr<MockBroker>    broker   = std::make_shared<MockBroker>();
    std::shared_ptr<MockStrategy>  strategy = std::make_shared<MockStrategy>();
    std::shared_ptr<RiskManager>   risk     = std::make_shared<RiskManager>();
    std::shared_ptr<NullLogger>    logger   = std::make_shared<NullLogger>();
    std::unique_ptr<TradingBot>    bot;

    explicit BotHarness(double lastClose = 420.0, const std::string& lastDate = "2024-03-01") {
        feed->setBars(barsEndingAt(lastClose, lastDate));
        bot = std::make_unique<TradingBot>(feed, broker, strategy, risk, logger);
    }

    // 60 barres plates qui se terminent au prix/date voulus
    static std::vector<Bar> barsEndingAt(double close, const std::string& date) {
        auto bars = MockDataFeed::buildTrend(close, 59, 0.0);
        Bar last;
        last.date  = date;
        last.open  = close; last.high = close + 1; last.low = close - 1;
        last.close = close; last.volume = 1'000'000;
        bars.push_back(last);
        return bars;
    }

    void setLastBar(double close, const std::string& date) {
        feed->setBars(barsEndingAt(close, date));
    }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  Garde-fous de base
// ════════════════════════════════════════════════════════════

TEST(TradingBotUnit, MarketClosedDoesNothing) {
    BotHarness h;
    h.feed->setMarketOpen(false);
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 0);
    EXPECT_FALSE(h.bot->state().inPosition);
}

TEST(TradingBotUnit, EmptyFeedDoesNothing) {
    BotHarness h;
    h.feed->setBars({});
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 0);
}

// ════════════════════════════════════════════════════════════
//  Sprint 1 item 5 — cash insuffisant : aucun ordre ne doit partir
// ════════════════════════════════════════════════════════════

// BUG : positionSize retournait max(1, 0)=1 → achat d'1 action à 420 $
// avec 150 $ de cash. Aucun ordre ne doit être soumis.
TEST(TradingBotUnit, InsufficientCashSubmitsNoOrder) {
    BotHarness h(420.0, "2024-03-01");
    h.broker->setAccount({150.0, 150.0, "ACTIVE"});
    h.strategy->setSignal(SignalType::BUY);

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 0);
    EXPECT_FALSE(h.bot->state().inPosition);
}

// Cas nominal : cash suffisant → l'achat part avec le sizing calculé (9 actions)
TEST(TradingBotUnit, BuySignalWithSufficientCashSubmitsOrder) {
    BotHarness h(420.0, "2024-03-01");
    h.broker->setAccount({10'000.0, 10'000.0, "ACTIVE"});
    h.strategy->setSignal(SignalType::BUY);

    h.bot->runOnce();

    ASSERT_EQ(h.broker->buyCount(), 1);
    EXPECT_EQ(h.broker->orders().back().qty, 9);
    EXPECT_TRUE(h.bot->state().inPosition);
}
