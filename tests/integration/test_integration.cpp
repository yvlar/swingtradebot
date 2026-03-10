#include <gtest/gtest.h>
#include "bot/TradingBot.hpp"
#include "bot/Mocks.hpp"
#include "bot/Logger.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"

using namespace trading;
using namespace trading::mocks;

// ─── Fixture partagée ─────────────────────────────────────────────────────────
class TradingBotIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<MockDataFeed> dataFeed;
    std::shared_ptr<MockBroker>   broker;
    std::shared_ptr<NullLogger>   logger;
    std::unique_ptr<TradingBot>   bot;

    void SetUp() override {
        dataFeed = std::make_shared<MockDataFeed>();
        broker   = std::make_shared<MockBroker>();
        logger   = std::make_shared<NullLogger>();

        broker->setAccount({10000.0, 10000.0, "ACTIVE"});

        bot = std::make_unique<TradingBot>(
            dataFeed,
            broker,
            SwingStrategy::create(),
            std::make_shared<RiskManager>(),
            logger
        );
    }

    // Configure des barres avec une tendance haussière marquée (force un BUY)
    void setupBullishMarket() {
        // Longue baisse puis forte hausse → croisement EMA haussier
        std::vector<Bar> bars;
        double price = 430.0;
        // Phase 1: baisse lente (EMA slow > EMA fast)
        for (int i = 0; i < 25; ++i) {
            price -= 0.5;
            bars.push_back(makeBar(price, "2024-01-" + pad(i+1)));
        }
        // Phase 2: forte hausse soudaine (force croisement + RSI modéré)
        for (int i = 0; i < 5; ++i) {
            price += 4.0;
            bars.push_back(makeBar(price, "2024-02-" + pad(i+1)));
        }
        dataFeed->setBars(bars);
        dataFeed->setMarketOpen(true);
    }

    // Configure un marché baissier après une position ouverte
    void setupBearishMarket() {
        std::vector<Bar> bars;
        double price = 460.0;
        for (int i = 0; i < 25; ++i) {
            price -= 0.8;
            bars.push_back(makeBar(price, "2024-03-" + pad(i+1)));
        }
        dataFeed->setBars(bars);
    }

    static Bar makeBar(double price, const std::string& date) {
        Bar b;
        b.date   = date;
        b.open   = price - 0.5;
        b.high   = price + 1.0;
        b.low    = price - 1.0;
        b.close  = price;
        b.volume = 2'000'000;
        return b;
    }

    static std::string pad(int n) {
        return (n < 10 ? "0" : "") + std::to_string(n);
    }
};

// ═════════════════════════════════════════════════════════════════════════════
// TESTS D'INTÉGRATION
// ═════════════════════════════════════════════════════════════════════════════

// ── Marché fermé ──────────────────────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, DoesNothingWhenMarketClosed) {
    dataFeed->setMarketOpen(false);
    dataFeed->setBars(MockDataFeed::buildTrend(420.0, 40, 0.5));

    bot->runOnce();

    EXPECT_EQ(broker->buyCount(), 0);
    EXPECT_EQ(broker->sellCount(), 0);
    EXPECT_FALSE(bot->state().inPosition);
}

// ── Données insuffisantes ─────────────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, DoesNothingWithInsufficientData) {
    dataFeed->setMarketOpen(true);
    dataFeed->setBars(MockDataFeed::buildTrend(420.0, 5, 0.5));  // trop peu

    bot->runOnce();

    EXPECT_EQ(broker->buyCount(), 0);
    EXPECT_FALSE(bot->state().inPosition);
}

// ── Stop-loss déclenché ───────────────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, StopLossTriggersAutoSell) {
    // Simule une position ouverte à $420
    BotState state;
    state.inPosition = true;
    state.buyPrice   = 420.0;
    state.peakPrice  = 420.0;
    state.holdDays   = 5;  // au-delà du minimum
    bot->setState(state);

    // Position ouverte chez le broker
    Position pos;
    pos.symbol       = "QQQ";
    pos.shares       = 9;
    pos.avgPrice     = 420.0;
    pos.unrealizedPnl = -94.5;
    broker->setPosition(pos);

    // Prix actuel à $399 → baisse de 5% = stop-loss
    auto bars = MockDataFeed::buildTrend(399.0, 40, 0.0);
    dataFeed->setBars(bars);
    dataFeed->setMarketOpen(true);

    bot->runOnce();

    EXPECT_EQ(broker->sellCount(), 1);
    EXPECT_FALSE(bot->state().inPosition);
    EXPECT_EQ(bot->state().buyPrice, 0.0);
}

// ── Take-profit déclenché ─────────────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, TakeProfitTriggersAutoSell) {
    BotState state;
    state.inPosition = true;
    state.buyPrice   = 420.0;
    state.peakPrice  = 462.0;
    state.holdDays   = 6;
    bot->setState(state);

    Position pos;
    pos.symbol = "QQQ"; pos.shares = 9;
    pos.avgPrice = 420.0; pos.unrealizedPnl = 378.0;
    broker->setPosition(pos);

    // Prix à $462 → gain de 10% = take-profit
    auto bars = MockDataFeed::buildTrend(462.0, 40, 0.0);
    dataFeed->setBars(bars);
    dataFeed->setMarketOpen(true);

    bot->runOnce();

    EXPECT_EQ(broker->sellCount(), 1);
    EXPECT_FALSE(bot->state().inPosition);
}

// ── Pas de double achat ───────────────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, DoesNotBuyWhenAlreadyInPosition) {
    // Simule une position déjà ouverte
    BotState state;
    state.inPosition = true;
    state.buyPrice   = 420.0;
    state.peakPrice  = 425.0;
    state.holdDays   = 1;
    bot->setState(state);

    Position pos;
    pos.symbol = "QQQ"; pos.shares = 9; pos.avgPrice = 420.0;
    broker->setPosition(pos);

    // Marché haussier — normalement déclencherait un achat
    setupBullishMarket();
    bot->runOnce();

    // Aucun achat supplémentaire
    EXPECT_EQ(broker->buyCount(), 0);
}

// ── Rejet d'ordre par le broker ───────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, HandlesRejectedOrderGracefully) {
    broker->setRejectOrders(true);
    setupBullishMarket();

    bot->runOnce();

    // Le bot ne devrait pas crasher et l'état reste cohérent
    EXPECT_EQ(broker->buyCount(), 0);
}

// ── État réinitialisé après vente ─────────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, StateResetAfterSell) {
    BotState state;
    state.inPosition = true;
    state.buyPrice   = 420.0;
    state.peakPrice  = 420.0;
    state.holdDays   = 5;
    bot->setState(state);

    Position pos;
    pos.symbol = "QQQ"; pos.shares = 9; pos.avgPrice = 420.0; pos.unrealizedPnl = -94.5;
    broker->setPosition(pos);

    // Stop-loss déclenché
    auto bars = MockDataFeed::buildTrend(398.0, 40, 0.0);
    dataFeed->setBars(bars);
    dataFeed->setMarketOpen(true);
    bot->runOnce();

    // Vérification de la réinitialisation complète
    const auto& s = bot->state();
    EXPECT_FALSE(s.inPosition);
    EXPECT_EQ(s.buyPrice,  0.0);
    EXPECT_EQ(s.peakPrice, 0.0);
    EXPECT_EQ(s.holdDays,  0);
}

// ── Compte inactif bloque le trading ─────────────────────────────────────────

TEST_F(TradingBotIntegrationTest, InactiveAccountBlocksTrade) {
    broker->setAccount({10000.0, 10000.0, "SUSPENDED"});
    setupBullishMarket();

    bot->runOnce();

    EXPECT_EQ(broker->buyCount(), 0);
    EXPECT_FALSE(bot->state().inPosition);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
