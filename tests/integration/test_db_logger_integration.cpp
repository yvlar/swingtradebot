// ============================================================
//  test_db_logger_integration.cpp  —  Tests INTÉGRATION
//  Cible : db_logger.h — cycles complets, persistance
// ============================================================
#include <gtest/gtest.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "core/db_logger.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Fixture ───────────────────────────────────────────────
class DbLoggerIntegration : public ::testing::Test {
protected:
    std::string db_path_;
    DbLogger*   db_ = nullptr;

    void SetUp() override {
        db_path_ = "integ_db_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
        db_ = new DbLogger(db_path_);
    }
    void TearDown() override {
        delete db_; db_ = nullptr;
        if (fs::exists(db_path_)) fs::remove(db_path_);
    }
};

TEST_F(DbLoggerIntegration, FullTradeCycle) {
    // 1. Log démarrage cycle
    db_->log({"INFO","Cycle #1 démarré","09:30:00"});

    // 2. Signal généré
    db_->record_signal({"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70}, 1);

    // 3. Ouverture position
    db_->record_trade("QQQ","long",10,472.30,466.60,483.70);
    db_->log({"OK","FILL QQQ LONG 10@472.30","09:30:01"});

    // 4. Snapshot equity après entrée
    db_->snapshot_equity(10000.0 - 10*472.30, 0.0, 1);

    // 5. TP atteint → fermeture
    db_->close_trade("QQQ", 483.70, 114.0);
    db_->log({"OK","CLOSE QQQ @ 483.70 PnL=+114.00","10:15:00"});

    // 6. Snapshot equity final
    db_->snapshot_equity(10114.0, 114.0, 1);

    // ── Vérifications ────────────────────────────────────
    json trades = json::parse(db_->trades_json());
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ   (trades[0]["symbol"], "QQQ");
    EXPECT_EQ   (trades[0]["status"], "closed");
    EXPECT_DOUBLE_EQ(trades[0]["pnl"].get<double>(), 114.0);
    EXPECT_DOUBLE_EQ(trades[0]["exit"].get<double>(), 483.70);

    json equity = json::parse(db_->equity_history_json());
    EXPECT_EQ(equity.size(), 2u);
    EXPECT_DOUBLE_EQ(equity.back()["equity"].get<double>(), 10114.0);
}

TEST_F(DbLoggerIntegration, PersistenceAcrossInstances) {
    // Écrit avec une instance
    db_->record_trade("QQQ","long",10,472.30,466.60,483.70);
    db_->snapshot_equity(10000.0, 0.0, 1);
    db_->log({"INFO","Avant restart","00:00:00"});
    delete db_; db_ = nullptr;

    // Relit avec une nouvelle instance sur le même fichier
    DbLogger db2(db_path_);
    json trades = json::parse(db2.trades_json());
    json equity = json::parse(db2.equity_history_json());

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(equity.size(), 1u);
    EXPECT_EQ(trades[0]["symbol"], "QQQ");

    db_ = new DbLogger(db_path_); // pour TearDown
}

TEST_F(DbLoggerIntegration, MultipleCyclesAccumulate) {
    for (int cycle = 1; cycle <= 10; ++cycle) {
        db_->log({"INFO","Cycle " + std::to_string(cycle),"00:00:00"});
        db_->record_signal({"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70}, cycle);
        db_->snapshot_equity(10000.0 + cycle*100, cycle*100.0, cycle);
    }
    json equity = json::parse(db_->equity_history_json());
    EXPECT_EQ(equity.size(), 10u);
    EXPECT_DOUBLE_EQ(equity.back()["equity"].get<double>(), 11000.0);
}

TEST_F(DbLoggerIntegration, MultipleTradesOpenAndClose) {
    // Ouvre 3 trades
    db_->record_trade("QQQ","long", 10,472.30,466.60,483.70);
    db_->record_trade("SPY","long",  5,520.00,514.00,532.00);
    db_->record_trade("NVDA","short",2,650.00,658.00,630.00);

    // Ferme 2 sur 3
    db_->close_trade("QQQ",  483.70,  114.0);
    db_->close_trade("NVDA", 630.00,   40.0);

    json trades = json::parse(db_->trades_json());
    ASSERT_EQ(trades.size(), 3u);

    int closed = 0, open = 0;
    for (const auto& t : trades) {
        if (t["status"] == "closed") closed++;
        else open++;
    }
    EXPECT_EQ(closed, 2);
    EXPECT_EQ(open,   1);
}

TEST_F(DbLoggerIntegration, StopLossAndTakeProfitScenario) {
    // Trade 1 : TP atteint (+114$)
    db_->record_trade("QQQ","long",10,472.30,466.60,483.70);
    db_->close_trade("QQQ", 483.70, 114.0);

    // Trade 2 : SL atteint (-57$)
    db_->record_trade("SPY","long",5,520.00,514.00,532.00);
    db_->close_trade("SPY", 514.00, -30.0);

    json trades = json::parse(db_->trades_json());
    ASSERT_EQ(trades.size(), 2u);

    double total_pnl = 0;
    for (const auto& t : trades)
        total_pnl += t["pnl"].get<double>();
    EXPECT_DOUBLE_EQ(total_pnl, 84.0); // 114 - 30
}
