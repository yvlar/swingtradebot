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

// Construit un BotState de test champ par champ : les agrégats incomplets
// ({true, 400.0, …}) sont des warnings -Werror et figeaient l'ordre des
// champs de la struct (fragilité notée dans Models.hpp).
BotState makeBotState(bool inPos, double buy, double peak, int hold,
                      const std::string& lastBarDate = "") {
    BotState s;
    s.inPosition  = inPos;
    s.buyPrice    = buy;
    s.peakPrice   = peak;
    s.holdDays    = hold;
    s.lastBarDate = lastBarDate;
    return s;
}

// ── Harnais : TradingBot câblé sur mocks, accès direct aux collaborateurs ──
struct BotHarness {
    std::shared_ptr<MockDataFeed>   feed     = std::make_shared<MockDataFeed>();
    std::shared_ptr<MockBroker>     broker   = std::make_shared<MockBroker>();
    std::shared_ptr<MockStrategy>   strategy = std::make_shared<MockStrategy>();
    std::shared_ptr<RiskManager>    risk     = std::make_shared<RiskManager>();
    std::shared_ptr<NullLogger>     logger   = std::make_shared<NullLogger>();
    std::shared_ptr<MockStateStore> store    = std::make_shared<MockStateStore>();
    std::unique_ptr<TradingBot>     bot;

    explicit BotHarness(double lastClose = 420.0, const std::string& lastDate = "2024-03-01") {
        feed->setBars(barsEndingAt(lastClose, lastDate));
        bot = std::make_unique<TradingBot>(feed, broker, strategy, risk, logger, store);
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

// Compte non ACTIF (ex : résumé IBKR incomplet → INACTIVE) : aucune entrée,
// et la cause est signalée en ERROR — pas le trompeur « Cash insuffisant »
TEST(TradingBotUnit, InactiveAccountLogsExplicitErrorAndSubmitsNoOrder) {
    auto feed     = std::make_shared<MockDataFeed>();
    auto broker   = std::make_shared<MockBroker>();
    auto strategy = std::make_shared<MockStrategy>();
    auto logger   = std::make_shared<MockLogger>();
    feed->setBars(BotHarness::barsEndingAt(420.0, "2024-03-01"));
    broker->setAccount({0.0, 0.0, "INACTIVE"});
    strategy->setSignal(SignalType::BUY);
    TradingBot bot(feed, broker, strategy,
                   std::make_shared<RiskManager>(), logger);

    bot.runOnce();

    EXPECT_EQ(broker->buyCount(), 0);
    EXPECT_TRUE(MockLogger::contains(logger->errors(), "non ACTIF"));
    EXPECT_FALSE(MockLogger::contains(logger->warns(), "Cash insuffisant"));
}

// B3.2 : feed plus court que le lookback demandé → WARN explicite (le filtre
// de régime SMA200 peut être incalculable → aucune entrée, il faut le voir)
TEST(TradingBotUnit, ShortHistoryLogsWarn) {
    auto feed     = std::make_shared<MockDataFeed>();
    auto broker   = std::make_shared<MockBroker>();
    auto strategy = std::make_shared<MockStrategy>();
    auto logger   = std::make_shared<MockLogger>();
    feed->setBars(BotHarness::barsEndingAt(420.0, "2024-03-01"));  // 60 barres
    TradingBot bot(feed, broker, strategy,
                   std::make_shared<RiskManager>(), logger);
    RiskConfig cfg;
    cfg.lookback = 230;                              // SMA200 → ~230 demandées
    bot.setConfig(cfg);

    bot.runOnce();

    EXPECT_TRUE(MockLogger::contains(logger->warns(), "Historique incomplet"));
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

// ════════════════════════════════════════════════════════════
//  Sprint 1 item 2 — le statut d'ordre doit être vérifié
// ════════════════════════════════════════════════════════════

// BUG : un ordre REJECTED mettait quand même le bot « en position »
TEST(TradingBotUnit, RejectedBuyDoesNotEnterPosition) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::REJECTED);

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 1);          // l'ordre a bien été tenté
    EXPECT_FALSE(h.bot->state().inPosition);     // mais l'état ne doit pas changer
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 0.0);
}

// BUG : un ordre PENDING (non encore exécuté) était traité comme un fill
// au prix du signal. Il ne doit pas changer l'état : la réconciliation avec
// la position broker fera foi au cycle suivant.
TEST(TradingBotUnit, PendingBuyDoesNotEnterPositionYet) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::PENDING);

    h.bot->runOnce();

    EXPECT_FALSE(h.bot->state().inPosition);
}

// BUG : buyPrice prenait le prix du SIGNAL (close) et non le prix de FILL
TEST(TradingBotUnit, BuyUsesFillPriceNotSignalPrice) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setFillPrice(415.0);               // slippage favorable de 5 $

    h.bot->runOnce();

    ASSERT_TRUE(h.bot->state().inPosition);
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice,  415.0);
    EXPECT_DOUBLE_EQ(h.bot->state().peakPrice, 415.0);
}

// BUG : une vente REJECTED réinitialisait quand même l'état → position
// réelle conservée chez le broker mais plus suivie par le bot (orpheline)
TEST(TradingBotUnit, RejectedSellKeepsPosition) {
    BotHarness h(368.0, "2024-03-05");           // -8 % sous le buyPrice → stop-loss
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 368.0, 9 * -32.0});
    h.bot->setState(makeBotState(true, 400.0, 405.0, 1));
    h.broker->setSubmitResult(OrderStatus::REJECTED);

    h.bot->runOnce();

    EXPECT_EQ(h.broker->sellCount(), 1);         // la vente a été tentée (stop-loss)
    EXPECT_TRUE(h.bot->state().inPosition);      // l'état doit rester « en position »
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 400.0);
}

// Échec de soumission (nullopt) : même exigence — l'état ne bouge pas
TEST(TradingBotUnit, FailedSellSubmissionKeepsPosition) {
    BotHarness h(368.0, "2024-03-05");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 368.0, 9 * -32.0});
    h.bot->setState(makeBotState(true, 400.0, 405.0, 1));
    h.broker->setSubmitResult(std::nullopt);

    h.bot->runOnce();

    EXPECT_TRUE(h.bot->state().inPosition);
}

// ════════════════════════════════════════════════════════════
//  Sprint 1 item 3 — holdDays = jours de bourse réels, pas cycles
// ════════════════════════════════════════════════════════════

// BUG : holdDays était incrémenté à CHAQUE runOnce (boucle prod = 60 min)
// → ~7 « jours » par jour de bourse. Il ne doit avancer qu'au changement
// de date de barre.
TEST(TradingBotUnit, HoldDaysDoesNotIncrementWithinSameBarDate) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                            // entrée le 2024-03-01
    ASSERT_TRUE(h.bot->state().inPosition);

    h.strategy->setSignal(SignalType::HOLD);
    h.bot->runOnce();                            // même journée (cycle 60 min)
    h.bot->runOnce();                            // même journée encore

    EXPECT_EQ(h.bot->state().holdDays, 0);       // aucun jour de bourse écoulé
}

TEST(TradingBotUnit, HoldDaysIncrementsOncePerNewBarDate) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                            // entrée le 2024-03-01

    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(420.0, "2024-03-04");           // lundi suivant
    h.bot->runOnce();
    h.bot->runOnce();                            // 2e cycle du même jour
    EXPECT_EQ(h.bot->state().holdDays, 1);

    h.setLastBar(420.0, "2024-03-05");
    h.bot->runOnce();
    EXPECT_EQ(h.bot->state().holdDays, 2);
}

// BUG conséquent : minHoldDays était consommé en quelques heures de cycles.
// Un signal SELL le jour même de l'achat ne doit PAS vendre avant
// minHoldDays jours de bourse réels.
TEST(TradingBotUnit, SellSignalWaitsForRealTradingDays) {
    BotHarness h(402.0, "2024-03-05");
    RiskConfig cfg;
    cfg.minHoldDays = 2;
    h.bot->setConfig(cfg);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 402.0, 9 * 2.0});
    h.bot->setState(makeBotState(true, 400.0, 402.0, 0, "2024-03-05"));
    h.strategy->setSignal(SignalType::SELL);

    h.bot->runOnce();                            // 3 cycles le même jour
    h.bot->runOnce();
    h.bot->runOnce();
    EXPECT_EQ(h.broker->sellCount(), 0);         // minHold pas atteint en vrais jours

    h.setLastBar(402.0, "2024-03-06");           // J+1
    h.bot->runOnce();
    EXPECT_EQ(h.broker->sellCount(), 0);         // holdDays=1 < 2

    h.setLastBar(402.0, "2024-03-07");           // J+2
    h.bot->runOnce();
    EXPECT_EQ(h.broker->sellCount(), 1);         // holdDays=2 → vente autorisée
}

// ════════════════════════════════════════════════════════════
//  Sprint 1 item 1 — réconciliation état ↔ position broker
// ════════════════════════════════════════════════════════════

// BUG : après un redémarrage, l'état mémoire est vierge (inPosition=false)
// alors que le broker détient une position → la branche de sortie ne
// s'exécutait plus jamais : position SANS stop-loss pour toujours.
TEST(TradingBotUnit, RestartAdoptsExistingBrokerPosition) {
    BotHarness h(410.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 410.0, 9 * 10.0});

    h.bot->runOnce();                            // 1er cycle après « restart »

    EXPECT_TRUE(h.bot->state().inPosition);      // position adoptée
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 400.0);  // prix moyen broker
}

// Adoption d'une position dont le broker ne renvoie pas de prix moyen
// (avgPrice=0, ex. réponse partielle) : le prix courant sert de base de coût
// (TradingBot.hpp:249). Sinon buyPrice=0 désactiverait silencieusement les
// stops (checkExitConditions:72 retourne nullopt si buyPrice<=0).
TEST(TradingBotUnit, AdoptedPositionWithZeroAvgPriceUsesCurrentPrice) {
    BotHarness h(410.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 0.0, 9 * 410.0, 0.0});  // avgPrice=0

    h.bot->runOnce();                            // adoption au prix courant

    EXPECT_TRUE(h.bot->state().inPosition);
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice,  410.0);  // repli sur le prix courant
    EXPECT_DOUBLE_EQ(h.bot->state().peakPrice, 410.0);
}

// La position adoptée doit être protégée : le stop-loss redevient actif
TEST(TradingBotUnit, AdoptedPositionStopLossFires) {
    BotHarness h(410.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 410.0, 9 * 10.0});
    h.bot->runOnce();                            // adoption
    ASSERT_TRUE(h.bot->state().inPosition);

    h.setLastBar(368.0, "2024-03-11");           // -8 % sous le prix moyen
    h.bot->runOnce();

    EXPECT_EQ(h.broker->sellCount(), 1);         // stop-loss déclenché
    EXPECT_FALSE(h.bot->state().inPosition);
}

// Sens inverse (découverte D3) : l'état dit « en position » mais le broker
// n'a plus rien (fermée à la main) → l'état restait bloqué pour toujours,
// plus aucune entrée possible.
TEST(TradingBotUnit, PositionGoneAtBrokerResetsState) {
    BotHarness h(420.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.bot->setState(makeBotState(true, 400.0, 420.0, 3, "2024-03-07"));
    h.broker->setPosition(std::nullopt);

    h.bot->runOnce();

    EXPECT_FALSE(h.bot->state().inPosition);     // état réconcilié
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 0.0);
}

// L'état persisté est rechargé au premier cycle (redémarrage avec store)
TEST(TradingBotUnit, StateRestoredFromStoreOnFirstCycle) {
    BotHarness h(404.0, "2024-03-08");
    h.store->preload(makeBotState(true, 400.0, 405.0, 2, "2024-03-07"));
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 404.0, 9 * 4.0});
    h.strategy->setSignal(SignalType::HOLD);

    h.bot->runOnce();

    EXPECT_TRUE(h.bot->state().inPosition);
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 400.0);
    EXPECT_EQ(h.bot->state().holdDays, 3);       // 2 persistés + nouvelle date
}

// Chaque mutation d'état (entrée, sortie) doit être persistée
TEST(TradingBotUnit, EntryAndExitArePersisted) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                            // achat

    ASSERT_TRUE(h.store->lastSaved().has_value());
    EXPECT_TRUE(h.store->lastSaved()->inPosition);
    EXPECT_DOUBLE_EQ(h.store->lastSaved()->buyPrice, 420.0);

    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(368.0, "2024-03-05");           // -12 % → stop-loss
    h.bot->runOnce();

    ASSERT_TRUE(h.store->lastSaved().has_value());
    EXPECT_FALSE(h.store->lastSaved()->inPosition);  // reset persisté
}

// Cas nominal : vente FILLED sur stop-loss → état réinitialisé
TEST(TradingBotUnit, FilledSellOnStopLossResetsState) {
    BotHarness h(368.0, "2024-03-05");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 368.0, 9 * -32.0});
    h.bot->setState(makeBotState(true, 400.0, 405.0, 1));

    h.bot->runOnce();

    EXPECT_EQ(h.broker->sellCount(), 1);
    EXPECT_FALSE(h.bot->state().inPosition);
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Sprint 2 item 10 — panne ≠ donnée vide (canal d'erreur Result)
// ════════════════════════════════════════════════════════════

// BUG : getPosition()→nullopt signifiait à la fois « pas de position »
// (certitude) et « panne réseau » (inconnue). Sur panne, la réconciliation
// réinitialisait l'état → stops désactivés sur une position bien réelle.
TEST(TradingBotUnit, BrokerOutageDoesNotResetPositionState) {
    BotHarness h(420.0, "2024-03-05");
    h.strategy->setSignal(SignalType::HOLD);
    h.bot->setState(makeBotState(true, 400.0, 405.0, 1, "2024-03-04"));
    h.broker->setPositionQueryFails("timeout simulé");

    h.bot->runOnce();

    EXPECT_TRUE(h.bot->state().inPosition);      // état CONSERVÉ
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 400.0);
    EXPECT_EQ(h.broker->sellCount(), 0);         // pas de trade à l'aveugle
}

// Symétrique : panne pendant que le broker détient une position non suivie
// → pas d'adoption hasardeuse (la réponse n'est pas certaine), pas d'achat
TEST(TradingBotUnit, BrokerOutageDoesNotAdoptNorBuy) {
    BotHarness h(420.0, "2024-03-05");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setPositionQueryFails("connexion refusée");

    h.bot->runOnce();

    EXPECT_FALSE(h.bot->state().inPosition);
    EXPECT_EQ(h.broker->buyCount(), 0);          // cycle ignoré
}

// Après la panne, le cycle suivant (broker rétabli) réconcilie normalement
TEST(TradingBotUnit, RecoveryAfterBrokerOutageReconcilesNormally) {
    BotHarness h(420.0, "2024-03-05");
    h.strategy->setSignal(SignalType::HOLD);
    h.bot->setState(makeBotState(true, 400.0, 405.0, 1, "2024-03-04"));
    h.broker->setPositionQueryFails("timeout simulé");
    h.bot->runOnce();
    ASSERT_TRUE(h.bot->state().inPosition);

    // Broker rétabli : il confirme qu'AUCUNE position n'existe
    h.broker->setPositionQueryOk();
    h.broker->setPosition(std::nullopt);
    h.bot->runOnce();

    EXPECT_FALSE(h.bot->state().inPosition);     // réconciliation légitime
}

// Panne du data feed : cycle ignoré sans toucher à l'état ni trader
TEST(TradingBotUnit, FeedOutageSkipsCycleWithoutTrading) {
    BotHarness h(420.0, "2024-03-05");
    h.strategy->setSignal(SignalType::BUY);
    h.feed->setFeedDown("DNS injoignable");

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 0);
    EXPECT_FALSE(h.bot->state().inPosition);
}

// Une panne feed n'est pas un « feed vide » : au rétablissement, le bot
// reprend le trading normalement
TEST(TradingBotUnit, FeedRecoveryResumesTrading) {
    BotHarness h(420.0, "2024-03-05");
    h.strategy->setSignal(SignalType::BUY);
    h.feed->setFeedDown("DNS injoignable");
    h.bot->runOnce();
    ASSERT_EQ(h.broker->buyCount(), 0);

    h.feed->setFeedUp();
    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 1);
    EXPECT_TRUE(h.bot->state().inPosition);
}

// ════════════════════════════════════════════════════════════
//  Compléments de couverture — refus du risk manager,
//  persistance en échec, statut d'ordre annulé
// ════════════════════════════════════════════════════════════

// Compte non ACTIVE : le sizing passe mais isTradeAllowed refuse → aucun ordre
TEST(TradingBotUnit, TradeNotAllowedByRiskManagerSubmitsNoOrder) {
    BotHarness h;
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setAccount({10'000.0, 10'000.0, "SUSPENDED"});

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 0);
}

// L'échec de persistance est consigné mais ne doit JAMAIS bloquer le trading
TEST(TradingBotUnit, SaveStateFailureDoesNotBlockTrading) {
    BotHarness h;
    h.store->setSaveFails(true);
    h.strategy->setSignal(SignalType::BUY);

    EXPECT_NO_THROW(h.bot->runOnce());
    EXPECT_EQ(h.broker->buyCount(), 1);
    EXPECT_TRUE(h.store->saved().empty());
}

// Ordre annulé par le broker : pas d'entrée en position, nouvel essai possible
TEST(TradingBotUnit, CancelledBuyDoesNotEnterPosition) {
    BotHarness h;
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::CANCELLED);

    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 1);

    // Toujours hors position : le signal BUY suivant retente l'achat
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 2);
}

// ════════════════════════════════════════════════════════════
//  Sprint 5 item 21 — observateur de cycle de vie des trades
//  (alimente DbLogger record_trade/close_trade + dashboard en prod)
// ════════════════════════════════════════════════════════════

// L'observateur reçoit un fill BUY à l'ouverture, puis un fill SELL à la
// clôture avec le P&L et la raison de sortie — uniquement sur fills confirmés.
TEST(TradingBotUnit, TradeObserverFiresOnEntryAndExit) {
    BotHarness h(420.0, "2024-03-01");
    std::vector<TradeFill> fills;
    h.bot->setTradeObserver([&](const TradeFill& f) { fills.push_back(f); });

    h.strategy->setSignal(SignalType::BUY);
    h.broker->setFillPrice(420.0);
    h.bot->runOnce();                                // achat @420

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].side,     OrderSide::BUY);
    EXPECT_EQ(fills[0].symbol,   "QQQ");
    EXPECT_EQ(fills[0].quantity, 9);
    EXPECT_DOUBLE_EQ(fills[0].price, 420.0);
    EXPECT_TRUE(fills[0].reason.empty());

    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(380.0, "2024-03-05");               // -9,5% → stop-loss
    h.broker->setFillPrice(380.0);
    h.bot->runOnce();                                // vente @380

    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[1].side,     OrderSide::SELL);
    EXPECT_EQ(fills[1].quantity, 9);
    EXPECT_DOUBLE_EQ(fills[1].price, 380.0);
    EXPECT_DOUBLE_EQ(fills[1].pnl, (380.0 - 420.0) * 9);   // P&L réel négatif
    EXPECT_NE(fills[1].reason.find("stop-loss"), std::string::npos);
}

// Un ordre NON exécuté (rejeté, en attente) ne doit PAS notifier l'observateur :
// le dashboard et la table trades ne reflètent que des fills réels.
TEST(TradingBotUnit, TradeObserverSilentOnUnfilledOrder) {
    BotHarness h(420.0, "2024-03-01");
    std::vector<TradeFill> fills;
    h.bot->setTradeObserver([&](const TradeFill& f) { fills.push_back(f); });

    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::REJECTED);
    h.bot->runOnce();

    EXPECT_TRUE(fills.empty());
}

// ════════════════════════════════════════════════════════════
//  Sprint 5 item 19 — stop résident côté broker (en COMPLÉMENT
//  du stop logiciel : décision « doubler »)
// ════════════════════════════════════════════════════════════

// À l'achat, un stop résident est déposé au broker au prix du stop-loss
// (fill × (1 - stopLossPct)) — en plus du stop logiciel.
TEST(TradingBotUnit, EntryPlacesResidentStopAtBroker) {
    BotHarness h(420.0, "2024-03-01");
    RiskConfig cfg;            // stopLossPct = 0.05 par défaut
    h.bot->setConfig(cfg);
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setFillPrice(420.0);

    h.bot->runOnce();

    ASSERT_EQ(h.broker->stops().size(), 1u);
    EXPECT_EQ(h.broker->stops()[0].qty, 9);
    EXPECT_DOUBLE_EQ(h.broker->stops()[0].stopPrice, 420.0 * 0.95);  // 399
}

// À la sortie (stop-loss logiciel), le stop résident est annulé pour ne pas
// laisser un ordre orphelin se déclencher après coup.
TEST(TradingBotUnit, ExitCancelsResidentStop) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                           // entrée → stop résident déposé
    ASSERT_EQ(h.broker->stops().size(), 1u);
    ASSERT_EQ(h.broker->cancelStopCount(), 0);

    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(380.0, "2024-03-05");          // -9,5% → stop-loss logiciel
    h.broker->setFillPrice(380.0);
    h.bot->runOnce();                           // sortie → annulation du stop

    EXPECT_EQ(h.broker->sellCount(), 1);
    EXPECT_EQ(h.broker->cancelStopCount(), 1);
}

// Un ordre d'achat NON exécuté ne dépose aucun stop résident
TEST(TradingBotUnit, NoResidentStopWhenBuyNotFilled) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::REJECTED);

    h.bot->runOnce();

    EXPECT_TRUE(h.broker->stops().empty());
}

// ════════════════════════════════════════════════════════════
//  B1 — le stop résident doit s'armer aussi à l'ADOPTION.
//  BUG : submitStopLoss n'était appelé que dans la branche FILLED
//  de l'achat. Or IBKR répond typiquement Submitted → PENDING → la
//  position est adoptée par réconciliation au cycle suivant… sans
//  stop. La protection « hors-ligne » était inopérante dans le cas
//  nominal, et après chaque restart.
// ════════════════════════════════════════════════════════════

// Position broker adoptée après restart → stop résident armé sur le coût moyen
TEST(TradingBotUnit, AdoptedPositionArmsResidentStop) {
    BotHarness h(410.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 410.0, 9 * 10.0});

    h.bot->runOnce();                            // adoption

    ASSERT_EQ(h.broker->stops().size(), 1u);
    EXPECT_EQ(h.broker->stops()[0].qty, 9);
    EXPECT_DOUBLE_EQ(h.broker->stops()[0].stopPrice, 400.0 * 0.95);
}

// Achat PENDING (cas nominal IBKR) : pas de stop à la soumission, mais dès
// que la position se matérialise chez le broker, le stop s'arme — UNE fois.
TEST(TradingBotUnit, PendingBuyThenMaterializedPositionArmsStopOnce) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setSubmitResult(OrderStatus::PENDING);
    h.bot->runOnce();                            // achat PENDING
    ASSERT_TRUE(h.broker->stops().empty());
    ASSERT_FALSE(h.bot->state().inPosition);

    // L'ordre s'exécute entre deux cycles : la position apparaît chez le broker
    h.broker->setPosition(Position{"QQQ", 9, 420.0, 9 * 420.0, 0.0});
    h.strategy->setSignal(SignalType::HOLD);
    h.bot->runOnce();                            // réconciliation → adoption
    ASSERT_EQ(h.broker->stops().size(), 1u);

    h.bot->runOnce();                            // cycles suivants : idempotent
    h.bot->runOnce();
    EXPECT_EQ(h.broker->stops().size(), 1u);
}

// Jamais deux stops pour la même position, même après plusieurs cycles
TEST(TradingBotUnit, ResidentStopNotStackedAcrossCycles) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                            // entrée FILLED → 1 stop
    ASSERT_EQ(h.broker->stops().size(), 1u);

    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(425.0, "2024-03-04");
    h.bot->runOnce();
    h.setLastBar(428.0, "2024-03-05");
    h.bot->runOnce();

    EXPECT_EQ(h.broker->stops().size(), 1u);
}

// État restauré avec stopArmed=true : le stop existe déjà chez le broker,
// on ne le re-dépose PAS (sinon deux stops actifs → double vente)
TEST(TradingBotUnit, RestoredStateWithArmedStopDoesNotRearm) {
    BotHarness h(410.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    h.broker->setPosition(Position{"QQQ", 9, 400.0, 9 * 410.0, 9 * 10.0});
    BotState persisted = makeBotState(true, 400.0, 405.0, 2, "2024-03-07");
    persisted.stopArmed = true;                  // stop déjà déposé avant le restart
    h.store->preload(persisted);

    h.bot->runOnce();

    EXPECT_TRUE(h.broker->stops().empty());
}

// Échec du dépôt (panne réseau) : pas d'état menteur, nouvel essai au cycle
// suivant dès que le broker répond
TEST(TradingBotUnit, FailedStopArmIsRetriedNextCycle) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.broker->setStopSubmitFails(true);
    h.bot->runOnce();                            // entrée, dépôt du stop échoue
    ASSERT_TRUE(h.bot->state().inPosition);
    ASSERT_TRUE(h.broker->stops().empty());
    ASSERT_TRUE(h.store->lastSaved().has_value());
    EXPECT_FALSE(h.store->lastSaved()->stopArmed);

    h.broker->setStopSubmitFails(false);
    h.strategy->setSignal(SignalType::HOLD);
    h.bot->runOnce();                            // filet : retente et réussit
    ASSERT_EQ(h.broker->stops().size(), 1u);
    EXPECT_TRUE(h.store->lastSaved()->stopArmed);
}

// Position fermée hors bot (ou par le stop résident lui-même) : le stop
// broker est annulé — pas d'ordre orphelin qui se déclencherait après coup
TEST(TradingBotUnit, PositionClosedOutsideBotCancelsResidentStop) {
    BotHarness h(415.0, "2024-03-08");
    h.strategy->setSignal(SignalType::HOLD);
    BotState persisted = makeBotState(true, 400.0, 415.0, 2, "2024-03-07");
    persisted.stopArmed = true;
    h.store->preload(persisted);
    h.broker->setPosition(std::nullopt);         // plus de position broker

    h.bot->runOnce();                            // réconciliation → reset

    EXPECT_FALSE(h.bot->state().inPosition);
    EXPECT_EQ(h.broker->cancelStopCount(), 1);
}

// ════════════════════════════════════════════════════════════
//  M2 — cooldown de ré-entrée : pas de rachat le jour de bourse
//  d'une sortie. Sans cela (cycle prod 60 min + regimeReentry),
//  une sortie stop à 10h30 pouvait être suivie d'un rachat à
//  11h30 : churn, coûts, wash-trades.
// ════════════════════════════════════════════════════════════

// Sortie stop puis signal BUY le même jour → aucune ré-entrée ;
// le lendemain, l'entrée repart normalement.
TEST(TradingBotUnit, SameDayReentryAfterStopExitIsBlocked) {
    BotHarness h(420.0, "2024-03-01");
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                            // entrée
    ASSERT_EQ(h.broker->buyCount(), 1);

    h.setLastBar(380.0, "2024-03-01");           // -9,5 % → stop logiciel
    h.broker->setFillPrice(380.0);
    h.bot->runOnce();                            // sortie (même jour)
    ASSERT_EQ(h.broker->sellCount(), 1);
    ASSERT_FALSE(h.bot->state().inPosition);

    h.bot->runOnce();                            // signal BUY, même jour
    EXPECT_EQ(h.broker->buyCount(), 1);          // ré-entrée bloquée

    h.setLastBar(385.0, "2024-03-04");           // jour de bourse suivant
    h.broker->setFillPrice(385.0);
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 2);          // cooldown levé
}

// Position fermée HORS bot (stop résident, vente manuelle) : même churn à
// bloquer — la réconciliation pose aussi le cooldown du jour
TEST(TradingBotUnit, ReentryCooldownAppliesAfterPositionClosedOutsideBot) {
    BotHarness h(415.0, "2024-03-08");
    BotState persisted = makeBotState(true, 400.0, 415.0, 2, "2024-03-07");
    h.store->preload(persisted);
    h.broker->setPosition(std::nullopt);         // fermée hors bot
    h.strategy->setSignal(SignalType::BUY);

    h.bot->runOnce();                            // reset + tentative de ré-entrée

    EXPECT_FALSE(h.bot->state().inPosition);
    EXPECT_EQ(h.broker->buyCount(), 0);          // bloquée le jour même
}

// Le cooldown survit à un redémarrage (lastExitDate est persisté)
TEST(TradingBotUnit, ReentryCooldownSurvivesRestart) {
    BotHarness h(415.0, "2024-03-08");
    BotState persisted;                          // à plat, sortie déjà faite ce jour
    persisted.lastExitDate = "2024-03-08";
    h.store->preload(persisted);
    h.strategy->setSignal(SignalType::BUY);

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 0);
}

// Une sortie de la VEILLE ne bloque pas l'entrée du jour
TEST(TradingBotUnit, CooldownDoesNotBlockNextTradingDay) {
    BotHarness h(415.0, "2024-03-08");
    BotState persisted;
    persisted.lastExitDate = "2024-03-07";       // sortie hier
    h.store->preload(persisted);
    h.strategy->setSignal(SignalType::BUY);

    h.bot->runOnce();

    EXPECT_EQ(h.broker->buyCount(), 1);
}

// ════════════════════════════════════════════════════════════
//  Sprint 5 item 18 — kill-switch : coupe les ENTRÉES, jamais les sorties
// ════════════════════════════════════════════════════════════

// Plafond d'ordres journalier : une fois le quota du jour atteint, plus
// aucune nouvelle entrée le même jour (les ordres déjà passés comptent).
TEST(TradingBotUnit, KillSwitchBlocksEntryWhenDailyOrderCapReached) {
    BotHarness h(420.0, "2024-03-01");
    RiskConfig cfg;
    cfg.killSwitch.maxOrdersPerDay = 1;   // 1 seul ordre par jour
    h.bot->setConfig(cfg);

    // Ordre #1 du jour : achat → entre en position
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();
    ASSERT_EQ(h.broker->buyCount(), 1);
    ASSERT_TRUE(h.bot->state().inPosition);

    // Ordre #2 du jour : stop-loss → vente (même date) ; ordersToday = 2
    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(380.0, "2024-03-01");    // -9,5% → stop-loss, même jour
    h.broker->setFillPrice(380.0);
    h.bot->runOnce();
    ASSERT_EQ(h.broker->sellCount(), 1);
    ASSERT_FALSE(h.bot->state().inPosition);

    // Nouvelle tentative d'entrée le MÊME jour : plafond (1) déjà dépassé
    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(420.0, "2024-03-01");
    h.broker->setFillPrice(420.0);
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 1);   // aucune nouvelle entrée
    EXPECT_FALSE(h.bot->state().inPosition);

    // Le lendemain : compteur journalier réinitialisé → l'entrée repart
    h.setLastBar(420.0, "2024-03-04");
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), 2);
}

// Pertes consécutives : après N pertes d'affilée, plus d'entrée. La série
// est un compteur de tendance (pas journalier) : on enchaîne sur des jours
// distincts pour ne pas buter sur le plafond d'ordres.
TEST(TradingBotUnit, KillSwitchBlocksEntryAfterConsecutiveLosses) {
    BotHarness h(420.0, "2024-03-01");
    RiskConfig cfg;
    cfg.killSwitch.maxConsecutiveLosses = 2;   // 2 pertes → coupure
    cfg.killSwitch.maxOrdersPerDay      = 50;  // n'interfère pas ici
    h.bot->setConfig(cfg);

    // Deux trades perdants, chacun sur 2 jours (entrée puis stop-loss)
    int day = 1;
    auto date = [&](int d) {
        std::string s = "2024-03-";
        return s + (d < 10 ? "0" : "") + std::to_string(d);
    };
    for (int loss = 0; loss < 2; ++loss) {
        h.strategy->setSignal(SignalType::BUY);
        h.setLastBar(420.0, date(day));
        h.broker->setFillPrice(420.0);
        h.bot->runOnce();                       // entrée @420
        ASSERT_TRUE(h.bot->state().inPosition);

        h.strategy->setSignal(SignalType::HOLD);
        h.setLastBar(380.0, date(day + 1));     // -9,5% → stop-loss (perte)
        h.broker->setFillPrice(380.0);
        h.bot->runOnce();                       // sortie perdante
        ASSERT_FALSE(h.bot->state().inPosition);
        day += 2;
    }

    // 2 pertes consécutives atteintes → l'entrée suivante est coupée
    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(420.0, date(day));
    h.broker->setFillPrice(420.0);
    int buysBefore = h.broker->buyCount();
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), buysBefore);   // aucune nouvelle entrée
    EXPECT_FALSE(h.bot->state().inPosition);
}

// Un trade GAGNANT remet la série de pertes à zéro → l'entrée reste permise
TEST(TradingBotUnit, KillSwitchConsecutiveLossesResetByAWin) {
    BotHarness h(420.0, "2024-03-01");
    RiskConfig cfg;
    cfg.takeProfitPct = 0.10;  // explicite : le défaut est désormais 0 = désactivé (item 8.2)
    cfg.killSwitch.maxConsecutiveLosses = 2;
    cfg.killSwitch.maxOrdersPerDay      = 50;
    h.bot->setConfig(cfg);
    auto date = [](int d) {
        std::string s = "2024-03-";
        return s + (d < 10 ? "0" : "") + std::to_string(d);
    };

    // 1 perte
    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(420.0, date(1)); h.broker->setFillPrice(420.0); h.bot->runOnce();
    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(380.0, date(2)); h.broker->setFillPrice(380.0); h.bot->runOnce();
    ASSERT_FALSE(h.bot->state().inPosition);

    // 1 gain (take-profit ≥ +10%) → la série retombe à 0
    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(400.0, date(3)); h.broker->setFillPrice(400.0); h.bot->runOnce();
    ASSERT_TRUE(h.bot->state().inPosition);
    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(450.0, date(6)); h.broker->setFillPrice(450.0); h.bot->runOnce();  // +12,5%
    ASSERT_FALSE(h.bot->state().inPosition);

    // 1 perte de plus : série = 1 (et non 2) → entrée toujours permise
    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(420.0, date(7)); h.broker->setFillPrice(420.0); h.bot->runOnce();
    h.strategy->setSignal(SignalType::HOLD);
    h.setLastBar(380.0, date(8)); h.broker->setFillPrice(380.0); h.bot->runOnce();

    h.strategy->setSignal(SignalType::BUY);
    h.setLastBar(420.0, date(9)); h.broker->setFillPrice(420.0);
    int buysBefore = h.broker->buyCount();
    h.bot->runOnce();
    EXPECT_EQ(h.broker->buyCount(), buysBefore + 1);   // entrée autorisée
    EXPECT_TRUE(h.bot->state().inPosition);
}

// Vente PENDING (via stop-loss, prioritaire sur minHoldDays) : la position
// est conservée et la sortie sera retentée au cycle suivant
TEST(TradingBotUnit, PendingSellKeepsPosition) {
    BotHarness h;
    h.strategy->setSignal(SignalType::BUY);
    h.bot->runOnce();                                    // entre en position @420

    h.setLastBar(390.0, "2024-03-02");                   // -7 % → stop-loss
    h.broker->setSubmitResult(OrderStatus::PENDING);

    h.bot->runOnce();
    EXPECT_EQ(h.broker->sellCount(), 1);

    h.setLastBar(388.0, "2024-03-03");                   // toujours sous le stop
    h.bot->runOnce();                                    // position conservée → re-vend
    EXPECT_EQ(h.broker->sellCount(), 2);
}
