// ============================================================
//  test_execution_stats_unit.cpp  —  Tests UNITAIRES
//  Cible : instrumentation des signaux non exécutables
//  (Sprint 23, item 23.1) — seams setSignalObserver /
//  setEntryObserver de TradingBot + agrégateur ExecutionStats.
//  Chaque raison de rejet a SON test ; un signal donnant zéro
//  action est compté SANS créer d'ordre.
// ============================================================
#include <gtest/gtest.h>
#include <memory>
#include "backtest/ExecutionStats.hpp"
#include "bot/TradingBot.hpp"
#include "bot/RiskManager.hpp"
#include "bot/Mocks.hpp"

using namespace trading;
using namespace trading::mocks;

namespace {

// Bot instrumenté prêt à l'emploi : feed en tendance, stratégie forcée BUY,
// broker mock (cash configurable), vrai RiskManager, stats branchées.
struct Rig {
    std::shared_ptr<MockDataFeed>  feed   = std::make_shared<MockDataFeed>();
    std::shared_ptr<MockBroker>    broker = std::make_shared<MockBroker>();
    std::shared_ptr<MockStrategy>  strat  = std::make_shared<MockStrategy>();
    std::shared_ptr<RiskManager>   risk   = std::make_shared<RiskManager>();
    std::shared_ptr<MockLogger>    logger = std::make_shared<MockLogger>();
    std::unique_ptr<TradingBot>    bot;
    ExecutionStats                 stats;

    explicit Rig(double cash, double price,
                 double riskPct = 0.02, double stopPct = 0.05) {
        feed->setBars(MockDataFeed::buildTrend(price, 30, 0.0));
        broker->setAccount({cash, cash, "ACTIVE"});
        strat->setSignal(SignalType::BUY, "test");
        bot = std::make_unique<TradingBot>(feed, broker, strat, risk, logger);
        RiskConfig cfg;
        cfg.riskPerTradePct = riskPct;
        cfg.stopLossPct     = stopPct;
        bot->setConfig(cfg);
        bot->setSignalObserver([this](const Signal& s) { stats.recordSignal(s); });
        bot->setEntryObserver([this](const EntryDecision& d) { stats.recordEntry(d); });
    }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  Raisons de rejet — une raison, un test
// ════════════════════════════════════════════════════════════

// QUANTITÉ ZÉRO : 1 000 $, risque 1 %, stop 5 %, prix 500 $ → budget de
// risque 10 $ / risque par action 25 $ = 0,4 action → 0 entière. Le rejet
// est compté avec sa raison et AUCUN ordre n'est créé.
TEST(ExecutionStatsUnit, ZeroQuantityIsCountedWithoutCreatingAnOrder) {
    Rig rig(1'000.0, 500.0, /*risk=*/0.01, /*stop=*/0.05);
    rig.bot->runOnce();

    EXPECT_EQ(rig.stats.entriesAttempted,            1);
    EXPECT_EQ(rig.stats.entriesRejectedZeroQuantity, 1);
    EXPECT_EQ(rig.stats.entriesExecuted,             0);
    EXPECT_EQ(rig.stats.entriesRejectedInsufficientCash, 0);
    EXPECT_EQ(rig.broker->buyCount(), 0);   // aucun ordre émis
    ASSERT_EQ(rig.stats.decisions.size(), 1u);
    EXPECT_EQ(rig.stats.decisions.front().outcome,
              EntryOutcome::RejetQuantiteZero);
    EXPECT_EQ(rig.stats.decisions.front().shares, 0);
}

// CASH INSUFFISANT : le budget de risque autorise ≥ 1 action (risque 50 %)
// mais le plafond de capital utilisable (95 %) ne paie même pas UNE action
// (prix 410 $ > 0,95 × 400 $) → rejet « cash », pas « quantité zéro ».
TEST(ExecutionStatsUnit, InsufficientCashIsDistinguishedFromZeroQuantity) {
    Rig rig(400.0, 410.0, /*risk=*/0.50, /*stop=*/0.05);
    rig.bot->runOnce();

    EXPECT_EQ(rig.stats.entriesAttempted,                1);
    EXPECT_EQ(rig.stats.entriesRejectedInsufficientCash, 1);
    EXPECT_EQ(rig.stats.entriesRejectedZeroQuantity,     0);
    EXPECT_EQ(rig.broker->buyCount(), 0);
}

// RISK MANAGER : un gestionnaire de risque stub accepte le sizing mais
// refuse le trade → rejet « risk manager », aucun ordre.
namespace {
class RefusingRiskManager final : public IRiskManager {
public:
    using IRiskManager::positionSize;
    using IRiskManager::checkExitConditions;
    int positionSize(double, double, double, double) const override { return 5; }
    int positionSize(double c, double p, double s, double r,
                     const std::vector<Bar>&, double) const override {
        return positionSize(c, p, s, r);
    }
    bool isTradeAllowed(const Account&, const std::optional<Position>&,
                        double, int) const override { return false; }
    std::optional<std::string> checkExitConditions(
        double, double, int, double, double, double, double, int) const override {
        return std::nullopt;
    }
    std::optional<std::string> checkExitConditions(
        double, double, int, double, double, double, double, int,
        const std::vector<Bar>&, double) const override { return std::nullopt; }
    std::optional<std::string> checkExitConditions(
        double, double, int, double, double, double, double, int,
        const std::vector<Bar>&, double, int) const override { return std::nullopt; }
    std::optional<std::string> checkKillSwitch(
        const KillSwitchConfig&, double, double, int, int) const override {
        return std::nullopt;
    }
};
} // namespace

TEST(ExecutionStatsUnit, RiskManagerRefusalIsCountedWithItsOwnReason) {
    Rig rig(10'000.0, 100.0);
    auto botRefus = std::make_unique<TradingBot>(
        rig.feed, rig.broker, rig.strat,
        std::make_shared<RefusingRiskManager>(), rig.logger);
    botRefus->setConfig(RiskConfig{});
    ExecutionStats stats;
    botRefus->setEntryObserver([&stats](const EntryDecision& d) {
        stats.recordEntry(d);
    });
    botRefus->runOnce();

    EXPECT_EQ(stats.entriesAttempted,           1);
    EXPECT_EQ(stats.entriesRejectedRiskManager, 1);
    EXPECT_EQ(rig.broker->buyCount(), 0);
}

// ORDRE NON EXÉCUTÉ : la soumission échoue (panne simulée) → l'ordre a bien
// été tenté (buyCount = 1) mais le rejet est compté avec sa raison.
TEST(ExecutionStatsUnit, FailedOrderSubmissionIsCounted) {
    Rig rig(10'000.0, 100.0);
    rig.broker->setSubmitResult(std::nullopt);   // échec de soumission
    rig.bot->runOnce();

    EXPECT_EQ(rig.stats.entriesAttempted,           1);
    EXPECT_EQ(rig.stats.entriesRejectedOrderFailed, 1);
    EXPECT_EQ(rig.stats.entriesExecuted,            0);
    EXPECT_EQ(rig.broker->buyCount(), 1);
}

// EXÉCUTÉE : chemin nominal — l'entrée est comptée exécutée avec la
// quantité remplie.
TEST(ExecutionStatsUnit, ExecutedEntryIsCounted) {
    Rig rig(10'000.0, 100.0);   // risque 2 %/stop 5 % → 40 actions
    rig.bot->runOnce();

    EXPECT_EQ(rig.stats.entriesAttempted, 1);
    EXPECT_EQ(rig.stats.entriesExecuted,  1);
    EXPECT_EQ(rig.broker->buyCount(), 1);
    ASSERT_EQ(rig.stats.decisions.size(), 1u);
    EXPECT_EQ(rig.stats.decisions.front().outcome, EntryOutcome::Executee);
    EXPECT_EQ(rig.stats.decisions.front().shares, 40);
}

// ════════════════════════════════════════════════════════════
//  Compteurs de signaux
// ════════════════════════════════════════════════════════════

TEST(ExecutionStatsUnit, SignalsAreCountedByType) {
    ExecutionStats stats;
    Signal buy;  buy.type  = SignalType::BUY;
    Signal sell; sell.type = SignalType::SELL;
    Signal hold; hold.type = SignalType::HOLD;
    stats.recordSignal(buy);
    stats.recordSignal(buy);
    stats.recordSignal(sell);
    stats.recordSignal(hold);

    EXPECT_EQ(stats.buySignalsGenerated,  2);
    EXPECT_EQ(stats.sellSignalsGenerated, 1);
}

// Le seam signalObserver compte le signal du cycle même sans entrée (HOLD)
TEST(ExecutionStatsUnit, SignalObserverSeesEveryCycle) {
    Rig rig(10'000.0, 100.0);
    rig.strat->setSignal(SignalType::HOLD);
    rig.bot->runOnce();
    rig.strat->setSignal(SignalType::BUY);
    rig.bot->runOnce();

    EXPECT_EQ(rig.stats.buySignalsGenerated, 1);
    EXPECT_EQ(rig.stats.entriesAttempted,    1);   // seul le BUY tente une entrée
}

// ════════════════════════════════════════════════════════════
//  Métriques de déploiement — calcul manuel
// ════════════════════════════════════════════════════════════

// Deux barres : 400/1000 = 40 % puis 0/1000 = 0 % → moyenne 20 %,
// max 40 %, cash inutilisé moyen 80 %
TEST(ExecutionStatsUnit, DeploymentMetricsMatchManualComputation) {
    ExecutionStats stats;
    stats.recordBar(400.0, 1'000.0);
    stats.recordBar(0.0,   1'000.0);

    EXPECT_NEAR(stats.averageCapitalDeployedPct(), 20.0, 1e-9);
    EXPECT_NEAR(stats.maximumCapitalDeployedPct(), 40.0, 1e-9);
    EXPECT_NEAR(stats.averageIdleCashPct(),        80.0, 1e-9);
    EXPECT_EQ(stats.barsObserved, 2);
}

TEST(ExecutionStatsUnit, EmptyStatsAreAllZero) {
    ExecutionStats stats;
    EXPECT_EQ(stats.entriesAttempted, 0);
    EXPECT_NEAR(stats.averageCapitalDeployedPct(), 0.0, 1e-12);
    EXPECT_NEAR(stats.averageIdleCashPct(),      100.0, 1e-12);
}
