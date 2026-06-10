// ============================================================
//  test_db_logger_unit.cpp  —  Tests UNITAIRES
//  Cible : db_logger.h — opérations SQLite isolées
// ============================================================
#include <gtest/gtest.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "core/db_logger.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Fixture ───────────────────────────────────────────────
class DbLoggerUnit : public ::testing::Test {
protected:
    std::string db_path_;
    DbLogger*   db_ = nullptr;

    void SetUp() override {
        db_path_ = "unit_db_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
        db_ = new DbLogger(db_path_);
    }
    void TearDown() override {
        delete db_; db_ = nullptr;
        if (fs::exists(db_path_)) fs::remove(db_path_);
    }
};

// ── Init ──────────────────────────────────────────────────
TEST_F(DbLoggerUnit, DbFileCreated) {
    EXPECT_TRUE(fs::exists(db_path_));
}

TEST_F(DbLoggerUnit, InvalidPathThrows) {
    EXPECT_THROW(DbLogger("/invalid/path/bot.db"), std::runtime_error);
}

// ── Logs ──────────────────────────────────────────────────
TEST_F(DbLoggerUnit, LogInsertNoThrow) {
    EXPECT_NO_THROW(db_->log({"INFO","Test message","14:32:01"}));
}

TEST_F(DbLoggerUnit, LogAllLevels) {
    for (const auto& lvl : {"INFO","WARN","ERROR","OK"})
        EXPECT_NO_THROW(db_->log({lvl, "msg", "00:00:00"}));
}

TEST_F(DbLoggerUnit, LogSpecialChars) {
    EXPECT_NO_THROW(db_->log({"INFO","$10,000 — \"test\" <ok>","09:00:00"}));
}

TEST_F(DbLoggerUnit, Log200EntriesNoError) {
    for (int i = 0; i < 200; ++i)
        EXPECT_NO_THROW(db_->log({"INFO","msg " + std::to_string(i),"00:00:00"}));
}

// ── Trades ────────────────────────────────────────────────
TEST_F(DbLoggerUnit, RecordTradeNoThrow) {
    EXPECT_NO_THROW(db_->record_trade("QQQ","long",10,472.30,466.60,483.70));
}

TEST_F(DbLoggerUnit, RecordShortTradeNoThrow) {
    EXPECT_NO_THROW(db_->record_trade("SPY","short",5,520.00,526.00,507.00));
}

TEST_F(DbLoggerUnit, CloseExistingTradeNoThrow) {
    db_->record_trade("QQQ","long",10,472.30,466.60,483.70);
    EXPECT_NO_THROW(db_->close_trade("QQQ", 483.70, 114.0));
}

TEST_F(DbLoggerUnit, CloseNonExistentTradeNoThrow) {
    EXPECT_NO_THROW(db_->close_trade("INEXISTANT", 100.0, 0.0));
}

TEST_F(DbLoggerUnit, TradesJsonIsValidArray) {
    db_->record_trade("QQQ","long", 10,472.30,466.60,483.70);
    db_->record_trade("SPY","short", 5,520.00,526.00,507.00);
    json j = json::parse(db_->trades_json());
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2u);
}

TEST_F(DbLoggerUnit, TradesJsonContainsExpectedFields) {
    db_->record_trade("QQQ","long",10,472.30,466.60,483.70);
    json j = json::parse(db_->trades_json());
    ASSERT_GE(j.size(), 1u);
    EXPECT_EQ(j[0]["symbol"], "QQQ");
    EXPECT_EQ(j[0]["side"],   "long");
    EXPECT_EQ(j[0]["qty"],    10);
    EXPECT_DOUBLE_EQ(j[0]["entry"].get<double>(), 472.30);
}

TEST_F(DbLoggerUnit, TradesJsonLimitRespected) {
    for (int i = 0; i < 60; ++i)
        db_->record_trade("QQQ","long",i+1,472.30,466.60,483.70);
    json j = json::parse(db_->trades_json(20));
    EXPECT_LE(j.size(), 20u);
}

// ── Equity curve ──────────────────────────────────────────
TEST_F(DbLoggerUnit, SnapshotEquityNoThrow) {
    EXPECT_NO_THROW(db_->snapshot_equity(10250.0, 250.0, 1));
}

TEST_F(DbLoggerUnit, EquityHistoryJsonIsValidArray) {
    db_->snapshot_equity(10000.0,   0.0, 0);
    db_->snapshot_equity(10150.0, 150.0, 1);
    db_->snapshot_equity(10300.0, 300.0, 2);
    json j = json::parse(db_->equity_history_json());
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 3u);
}

TEST_F(DbLoggerUnit, EquityHistoryChronologicalOrder) {
    db_->snapshot_equity(10000.0,   0.0, 0);
    db_->snapshot_equity(10150.0, 150.0, 1);
    db_->snapshot_equity(10300.0, 300.0, 2);
    json j = json::parse(db_->equity_history_json());
    EXPECT_EQ(j[0]["cycle"].get<int>(), 0);
    EXPECT_EQ(j[2]["cycle"].get<int>(), 2);
}

TEST_F(DbLoggerUnit, EquityHistoryLimitRespected) {
    for (int i = 0; i < 120; ++i)
        db_->snapshot_equity(10000.0 + i, (double)i, i);
    json j = json::parse(db_->equity_history_json(50));
    EXPECT_LE(j.size(), 50u);
}

// ── Signaux ───────────────────────────────────────────────
TEST_F(DbLoggerUnit, RecordSignalNoThrow) {
    SignalData s{"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70};
    EXPECT_NO_THROW(db_->record_signal(s, 1));
}

TEST_F(DbLoggerUnit, RecordMultipleSignalsNoThrow) {
    db_->record_signal({"QQQ","LONG", 3,28.5,472.30,2.85,466.60,483.70}, 1);
    db_->record_signal({"SPY","SHORT",2,71.0,520.00,3.10,526.20,507.60}, 1);
    SUCCEED();
}

// ── Robustesse SQLite (Sprint 2, item 9 + D2) ─────────────
// Avant le fix : sqlite3_stmt* stmt non initialisé — si prepare échouait
// (table absente, DB corrompue…), bind/step/finalize s'exécutaient sur un
// pointeur indéterminé = UB/crash. Chaque prepare/step doit être vérifié.

namespace {
// Supprime les tables derrière le dos du DbLogger (connexion séparée)
// → tous les sqlite3_prepare_v2 suivants échouent
void dropAllTables(const std::string& path) {
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    char* err = nullptr;
    sqlite3_exec(raw,
        "DROP TABLE logs; DROP TABLE trades;"
        "DROP TABLE equity_curve; DROP TABLE signals;",
        nullptr, nullptr, &err);
    sqlite3_free(err);
    sqlite3_close(raw);
}
} // namespace

TEST_F(DbLoggerUnit, WritesAfterPrepareFailureReturnFalseWithoutCrash) {
    dropAllTables(db_path_);

    EXPECT_FALSE(db_->log({"INFO", "msg", "00:00:00"}));
    EXPECT_FALSE(db_->record_trade("QQQ", "long", 10, 472.30, 466.60, 483.70));
    EXPECT_FALSE(db_->close_trade("QQQ", 483.70, 114.0));
    EXPECT_FALSE(db_->snapshot_equity(10000.0, 0.0, 1));
    EXPECT_FALSE(db_->record_signal(
        {"QQQ", "LONG", 3, 28.5, 472.30, 2.85, 466.60, 483.70}, 1));
}

TEST_F(DbLoggerUnit, QueriesAfterPrepareFailureReturnEmptyArrayWithoutCrash) {
    dropAllTables(db_path_);

    EXPECT_EQ(db_->trades_json(), "[]");
    EXPECT_EQ(db_->equity_history_json(), "[]");
}

TEST_F(DbLoggerUnit, SuccessfulWritesReturnTrue) {
    EXPECT_TRUE(db_->log({"INFO", "msg", "00:00:00"}));
    EXPECT_TRUE(db_->record_trade("QQQ", "long", 10, 472.30, 466.60, 483.70));
    EXPECT_TRUE(db_->close_trade("QQQ", 483.70, 114.0));
    EXPECT_TRUE(db_->snapshot_equity(10000.0, 0.0, 1));
    EXPECT_TRUE(db_->record_signal(
        {"QQQ", "LONG", 3, 28.5, 472.30, 2.85, 466.60, 483.70}, 1));
}
