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
    h.bot->setState({true, 400.0, 405.0, 1});
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
    h.bot->setState({true, 400.0, 405.0, 1});
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
    h.bot->setState({true, 400.0, 402.0, 0, "2024-03-05"});
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
    h.bot->setState({true, 400.0, 420.0, 3, "2024-03-07"});
    h.broker->setPosition(std::nullopt);

    h.bot->runOnce();

    EXPECT_FALSE(h.bot->state().inPosition);     // état réconcilié
    EXPECT_DOUBLE_EQ(h.bot->state().buyPrice, 0.0);
}

// L'état persisté est rechargé au premier cycle (redémarrage avec store)
TEST(TradingBotUnit, StateRestoredFromStoreOnFirstCycle) {
    BotHarness h(404.0, "2024-03-08");
    h.store->preload({true, 400.0, 405.0, 2, "2024-03-07"});
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
    h.bot->setState({true, 400.0, 405.0, 1});

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
    h.bot->setState({true, 400.0, 405.0, 1, "2024-03-04"});
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
    h.bot->setState({true, 400.0, 405.0, 1, "2024-03-04"});
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
