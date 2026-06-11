// ============================================================
//  test_paper_broker_unit.cpp  —  Tests UNITAIRES
//  Cible : PaperBroker — comptabilité du broker simulé
//  Chaque chiffre du backtest golden transite par cette classe :
//  commissions, P&L, ajustement de quantité, cycle de position
// ============================================================
#include <gtest/gtest.h>
#include "brokers/PaperBroker.hpp"

using namespace trading;

namespace {

// Broker prêt à trader : prix et date courants déjà fixés
PaperBroker readyBroker(double capital, double commission = 0.0,
                        double price = 100.0, const std::string& date = "2024-01-02") {
    PaperBroker b(capital, commission);
    b.setCurrentPrice(price);
    b.setCurrentDate(date);
    return b;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  submitBuy — débit du cash, commission, ajustement de qty
// ════════════════════════════════════════════════════════════

TEST(PaperBrokerUnit, BuyWithoutCurrentPriceReturnsNullopt) {
    PaperBroker b(10'000.0);
    EXPECT_FALSE(b.submitBuy("QQQ", 10).has_value());
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0);
}

TEST(PaperBrokerUnit, BuyZeroOrNegativeQtyReturnsNullopt) {
    auto b = readyBroker(10'000.0);
    EXPECT_FALSE(b.submitBuy("QQQ",  0).has_value());
    EXPECT_FALSE(b.submitBuy("QQQ", -5).has_value());
    EXPECT_FALSE(b.inPosition());
}

// 10 actions à 100 $, commission 0,1 % → coût 1 001 $
TEST(PaperBrokerUnit, BuyDebitsCashIncludingCommission) {
    auto b = readyBroker(10'000.0, 0.001);
    auto o = b.submitBuy("QQQ", 10);

    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->status, OrderStatus::FILLED);
    EXPECT_EQ(o->side,   OrderSide::BUY);
    EXPECT_EQ(o->quantity, 10);
    EXPECT_DOUBLE_EQ(o->price, 100.0);
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0 - 1'001.0);
    EXPECT_TRUE(b.inPosition());
}

// Cash insuffisant pour la qty demandée → quantité réduite au possible
TEST(PaperBrokerUnit, BuyShrinksQtyToAvailableCash) {
    auto b = readyBroker(1'000.0);          // sans commission
    auto o = b.submitBuy("QQQ", 50);        // 50 × 100 $ = 5 000 $ > 1 000 $

    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->quantity, 10);             // 1 000 / 100
    EXPECT_DOUBLE_EQ(b.cash(), 0.0);
}

// Avec commission, la réduction tient compte du coût TOTAL par action
TEST(PaperBrokerUnit, BuyShrinkAccountsForCommission) {
    auto b = readyBroker(1'000.0, 0.001);
    auto o = b.submitBuy("QQQ", 50);

    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->quantity, 9);              // 1 000 / 100,1 = 9,99 → 9
    EXPECT_NEAR(b.cash(), 1'000.0 - 9 * 100.1, 1e-9);
}

TEST(PaperBrokerUnit, BuyReturnsNulloptWhenCannotAffordOneShare) {
    auto b = readyBroker(50.0);             // action à 100 $
    EXPECT_FALSE(b.submitBuy("QQQ", 10).has_value());
    EXPECT_DOUBLE_EQ(b.cash(), 50.0);
    EXPECT_FALSE(b.inPosition());
}

// Comportement figé : un rachat alors qu'on est déjà en position ÉCRASE
// la position (pas de moyenne) — hypothèse du backtester mono-position
TEST(PaperBrokerUnit, RebuyOverwritesExistingPosition) {
    auto b = readyBroker(10'000.0);
    b.submitBuy("QQQ", 5);                  // 5 @ 100 $

    b.setCurrentPrice(120.0);
    b.submitBuy("QQQ", 5);                  // 5 @ 120 $ — écrase

    auto pos = b.getPosition("QQQ");
    ASSERT_TRUE(pos.ok());
    ASSERT_TRUE(pos.value().has_value());
    EXPECT_EQ(pos.value()->shares, 5);
    EXPECT_DOUBLE_EQ(pos.value()->avgPrice, 120.0);
}

// ════════════════════════════════════════════════════════════
//  submitSell — TradeRecord, P&L net de commission
// ════════════════════════════════════════════════════════════

TEST(PaperBrokerUnit, SellWithoutPositionReturnsNullopt) {
    auto b = readyBroker(10'000.0);
    EXPECT_FALSE(b.submitSell("QQQ", 10).has_value());
    EXPECT_TRUE(b.trades().empty());
}

// Achat 10 @ 100 $, vente @ 110 $, commission 0,1 % :
//   P&L = (110-100)×10 − 110×10×0,001 = 100 − 1,1 = 98,90 $
TEST(PaperBrokerUnit, SellRecordsPnlNetOfSellCommission) {
    auto b = readyBroker(10'000.0, 0.001);
    b.submitBuy("QQQ", 10);

    b.setCurrentPrice(110.0);
    b.setCurrentDate("2024-02-15");
    auto o = b.submitSell("QQQ", 10);

    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->status, OrderStatus::FILLED);
    EXPECT_EQ(o->side,   OrderSide::SELL);

    ASSERT_EQ(b.trades().size(), 1u);
    const auto& t = b.trades().front();
    EXPECT_NEAR(t.pnl, 98.9, 1e-9);
    EXPECT_DOUBLE_EQ(t.pnlPct, 10.0);       // pnlPct est brut (hors commission)
    EXPECT_TRUE(t.isWin);
    EXPECT_EQ(t.shares, 10);
    EXPECT_DOUBLE_EQ(t.buyPrice, 100.0);
    EXPECT_DOUBLE_EQ(t.sellPrice, 110.0);

    // Cash : 10 000 − 1 001 (achat) + 1 100×0,999 (vente) = 10 097,90
    EXPECT_NEAR(b.cash(), 10'097.9, 1e-9);
    EXPECT_FALSE(b.inPosition());
}

TEST(PaperBrokerUnit, LosingTradeIsNotWin) {
    auto b = readyBroker(10'000.0);
    b.submitBuy("QQQ", 10);

    b.setCurrentPrice(90.0);
    b.submitSell("QQQ", 10);

    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_DOUBLE_EQ(b.trades().front().pnl, -100.0);
    EXPECT_FALSE(b.trades().front().isWin);
}

// Aller-retour au même prix avec commission → P&L négatif → pas un win
// (isWin exige pnl strictement > 0)
TEST(PaperBrokerUnit, FlatTradeWithCommissionIsLoss) {
    auto b = readyBroker(10'000.0, 0.001);
    b.submitBuy("QQQ", 10);
    b.submitSell("QQQ", 10);                // même prix : 100 $

    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_LT(b.trades().front().pnl, 0.0);
    EXPECT_FALSE(b.trades().front().isWin);
}

TEST(PaperBrokerUnit, TradeRecordCarriesDatesAndExitReason) {
    auto b = readyBroker(10'000.0, 0.0, 100.0, "2024-01-02");
    b.submitBuy("QQQ", 10);

    b.setCurrentPrice(105.0);
    b.setCurrentDate("2024-01-20");
    b.setLastExitReason("stop-loss (-5.0%)");
    b.submitSell("QQQ", 10);

    ASSERT_EQ(b.trades().size(), 1u);
    const auto& t = b.trades().front();
    EXPECT_EQ(t.buyDate,  "2024-01-02");
    EXPECT_EQ(t.sellDate, "2024-01-20");
    EXPECT_NE(t.exitReason.find("stop-loss"), std::string::npos);
}

// holdDays : incrémenté seulement en position, capturé dans le trade,
// remis à zéro après la vente
TEST(PaperBrokerUnit, HoldDaysLifecycle) {
    auto b = readyBroker(10'000.0);
    b.incrementHoldDays();                  // hors position : sans effet
    b.submitBuy("QQQ", 10);
    b.incrementHoldDays();
    b.incrementHoldDays();
    b.incrementHoldDays();
    b.submitSell("QQQ", 10);

    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_EQ(b.trades().front().holdDays, 3);

    // Le trade suivant repart de zéro
    b.submitBuy("QQQ", 10);
    b.incrementHoldDays();
    b.submitSell("QQQ", 10);
    ASSERT_EQ(b.trades().size(), 2u);
    EXPECT_EQ(b.trades().back().holdDays, 1);
}

// ════════════════════════════════════════════════════════════
//  getPosition / getAccount / portfolioValue — valorisation
// ════════════════════════════════════════════════════════════

TEST(PaperBrokerUnit, GetPositionOkNulloptWhenFlat) {
    auto b = readyBroker(10'000.0);
    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());                    // broker simulé : jamais de panne
    EXPECT_FALSE(r.value().has_value());
}

TEST(PaperBrokerUnit, GetPositionRefreshesMarketValueAndUnrealizedPnl) {
    auto b = readyBroker(10'000.0);
    b.submitBuy("QQQ", 10);                 // 10 @ 100 $
    b.setCurrentPrice(105.0);

    auto r = b.getPosition("QQQ");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().has_value());
    EXPECT_DOUBLE_EQ(r.value()->marketValue,   1'050.0);
    EXPECT_DOUBLE_EQ(r.value()->unrealizedPnl,    50.0);
}

TEST(PaperBrokerUnit, AccountAndPortfolioValueIncludePosition) {
    auto b = readyBroker(10'000.0);
    b.submitBuy("QQQ", 10);                 // cash 9 000 + position 1 000
    b.setCurrentPrice(110.0);               // position vaut 1 100

    EXPECT_DOUBLE_EQ(b.portfolioValue(), 10'100.0);
    auto acc = b.getAccount();
    EXPECT_DOUBLE_EQ(acc.cash,   9'000.0);
    EXPECT_DOUBLE_EQ(acc.equity, 10'100.0);
    EXPECT_EQ(acc.status, "ACTIVE");
}

// ════════════════════════════════════════════════════════════
//  API backtester — equitySnapshot, closeOpenPosition
// ════════════════════════════════════════════════════════════

TEST(PaperBrokerUnit, EquitySnapshotAccumulatesCurveAndDates) {
    auto b = readyBroker(10'000.0);
    b.equitySnapshot(10'000.0, "2024-01-02");
    b.equitySnapshot(10'050.0, "2024-01-03");

    ASSERT_EQ(b.equityCurve().size(), 2u);
    ASSERT_EQ(b.equityDates().size(), 2u);
    EXPECT_DOUBLE_EQ(b.equityCurve()[1], 10'050.0);
    EXPECT_EQ(b.equityDates()[0], "2024-01-02");
}

TEST(PaperBrokerUnit, CloseOpenPositionSellsAllWithFinBacktestReason) {
    auto b = readyBroker(10'000.0);
    b.submitBuy("QQQ", 10);
    b.setCurrentPrice(108.0);
    b.closeOpenPosition();

    EXPECT_FALSE(b.inPosition());
    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_EQ(b.trades().front().exitReason, "fin-backtest");
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0 - 1'000.0 + 1'080.0);
}

TEST(PaperBrokerUnit, CloseOpenPositionIsNoopWhenFlat) {
    auto b = readyBroker(10'000.0);
    b.closeOpenPosition();
    EXPECT_TRUE(b.trades().empty());
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  Modèle de coûts réaliste (Sprint 6.2, D22) :
//  slippage + demi-spread paramétrables, en plus de la commission
// ════════════════════════════════════════════════════════════

// Achat : le fill est dégradé de (slippage + demi-spread) AU-DESSUS du close.
// 10 bps + 5 bps = 15 bps → 100 × 1,0015 = 100,15 $ ; le cash, la position
// et l'ordre reflètent tous le prix de fill, pas le close
TEST(PaperBrokerUnit, SlippageWorsensBuyFillPrice) {
    PaperBroker b(10'000.0, /*commission=*/0.0,
                  /*slippageBps=*/10.0, /*halfSpreadBps=*/5.0);
    b.setCurrentPrice(100.0);
    b.setCurrentDate("2024-01-02");

    auto o = b.submitBuy("QQQ", 10);
    ASSERT_TRUE(o.has_value());
    EXPECT_DOUBLE_EQ(o->price, 100.15);
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0 - 10 * 100.15);
}

// Vente : fill dégradé SOUS le close (100 × 0,9985 = 99,85 $) ; le P&L du
// trade est calculé sur les prix de fill — l'aller-retour au même close
// coûte exactement 2 × 15 bps
TEST(PaperBrokerUnit, SlippageWorsensSellFillAndTradePnl) {
    PaperBroker b(10'000.0, 0.0, 10.0, 5.0);
    b.setCurrentPrice(100.0);
    b.setCurrentDate("2024-01-02");
    b.submitBuy("QQQ", 10);

    auto o = b.submitSell("QQQ", 10);
    ASSERT_TRUE(o.has_value());
    EXPECT_DOUBLE_EQ(o->price, 99.85);

    ASSERT_EQ(b.trades().size(), 1u);
    const auto& t = b.trades().front();
    EXPECT_DOUBLE_EQ(t.buyPrice,  100.15);
    EXPECT_DOUBLE_EQ(t.sellPrice,  99.85);
    EXPECT_NEAR(t.pnl, (99.85 - 100.15) * 10, 1e-9);
    EXPECT_FALSE(t.isWin);
    EXPECT_DOUBLE_EQ(b.cash(), 10'000.0 - 10 * 100.15 + 10 * 99.85);
}

// Acceptation 6.2 : MÊMES trades, capital final strictement inférieur dès
// que le slippage est positif
TEST(PaperBrokerUnit, SameTradesEndWithLessCapitalWhenSlippagePositive) {
    PaperBroker sans(10'000.0, 0.001);             // commission seule
    PaperBroker avec(10'000.0, 0.001, 2.0, 0.5);   // + 2,5 bps par côté

    for (PaperBroker* b : {&sans, &avec}) {
        b->setCurrentPrice(100.0);
        b->setCurrentDate("2024-01-02");
        b->submitBuy("QQQ", 10);
        b->setCurrentPrice(110.0);
        b->setCurrentDate("2024-01-09");
        b->submitSell("QQQ", 10);
    }

    EXPECT_LT(avec.cash(), sans.cash());
    ASSERT_EQ(avec.trades().size(), 1u);
    ASSERT_EQ(sans.trades().size(), 1u);
    EXPECT_LT(avec.trades().front().pnl, sans.trades().front().pnl);
}

// Slippage et spread nuls (défaut) : comportement historique inchangé —
// fill exactement au close
TEST(PaperBrokerUnit, ZeroSlippageKeepsCloseFills) {
    PaperBroker b(10'000.0, 0.0, 0.0, 0.0);
    b.setCurrentPrice(100.0);
    b.setCurrentDate("2024-01-02");

    auto o = b.submitBuy("QQQ", 10);
    ASSERT_TRUE(o.has_value());
    EXPECT_DOUBLE_EQ(o->price, 100.0);
    EXPECT_DOUBLE_EQ(b.cash(), 9'000.0);
}
