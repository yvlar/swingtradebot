// ============================================================
//  test_state_store_unit.cpp  —  Tests UNITAIRES
//  Cible : SqliteStateStore — persistance de l'état de position
// ============================================================
#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include "core/state_store.h"

using namespace trading;
namespace fs = std::filesystem;

// ── Fixture : un fichier SQLite temporaire par test ───────
class StateStoreUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_state_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
    }
    void TearDown() override {
        for (const auto& suffix : {"", "-wal", "-shm"})
            if (fs::exists(path_ + suffix)) fs::remove(path_ + suffix);
    }

    static BotState sampleState() {
        return {true, 412.5, 418.0, 3, "2024-03-07"};
    }
};

TEST_F(StateStoreUnit, InvalidPathThrows) {
    EXPECT_THROW(SqliteStateStore("/chemin/inexistant/state.db"), std::runtime_error);
}

TEST_F(StateStoreUnit, LoadUnknownSymbolReturnsNullopt) {
    SqliteStateStore store(path_);
    EXPECT_FALSE(store.load("QQQ").has_value());
}

TEST_F(StateStoreUnit, SaveThenLoadRoundTrip) {
    SqliteStateStore store(path_);
    ASSERT_TRUE(store.save("QQQ", sampleState()));

    auto loaded = store.load("QQQ");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->inPosition);
    EXPECT_DOUBLE_EQ(loaded->buyPrice,  412.5);
    EXPECT_DOUBLE_EQ(loaded->peakPrice, 418.0);
    EXPECT_EQ(loaded->holdDays, 3);
    EXPECT_EQ(loaded->lastBarDate, "2024-03-07");
}

TEST_F(StateStoreUnit, SaveOverwritesPreviousState) {
    SqliteStateStore store(path_);
    ASSERT_TRUE(store.save("QQQ", sampleState()));
    ASSERT_TRUE(store.save("QQQ", BotState{}));      // position fermée

    auto loaded = store.load("QQQ");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->inPosition);
    EXPECT_DOUBLE_EQ(loaded->buyPrice, 0.0);
}

// Le scénario qui motive tout l'item 1 : l'état survit à un redémarrage
TEST_F(StateStoreUnit, StatePersistsAcrossReopen) {
    {
        SqliteStateStore store(path_);
        ASSERT_TRUE(store.save("QQQ", sampleState()));
    }                                                // « crash »/arrêt du bot
    SqliteStateStore reopened(path_);
    auto loaded = reopened.load("QQQ");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->inPosition);
    EXPECT_DOUBLE_EQ(loaded->buyPrice, 412.5);
}

TEST_F(StateStoreUnit, SymbolsAreIsolated) {
    SqliteStateStore store(path_);
    ASSERT_TRUE(store.save("QQQ", sampleState()));
    EXPECT_FALSE(store.load("SPY").has_value());
}

// ════════════════════════════════════════════════════════════
//  Branches d'erreur SQLite — fichier corrompu, schéma en conflit
// ════════════════════════════════════════════════════════════

// Un fichier qui n'est pas une base SQLite : l'open paresseux passe,
// c'est le CREATE TABLE du constructeur qui échoue → exception claire
TEST_F(StateStoreUnit, GarbageFileThrowsOnConstruction) {
    {
        std::ofstream f(path_);
        f << "ceci n'est pas une base sqlite";
    }
    EXPECT_THROW(SqliteStateStore store(path_), std::runtime_error);
}

// Table bot_state préexistante avec un schéma incompatible :
// les prepare échouent → load nullopt, save false — jamais de crash
TEST_F(StateStoreUnit, ConflictingSchemaFailsSoftly) {
    {
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(path_.c_str(), &raw), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw,
            "CREATE TABLE bot_state (symbol TEXT PRIMARY KEY);",
            nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(raw);
    }

    SqliteStateStore store(path_);   // CREATE IF NOT EXISTS : no-op silencieux
    EXPECT_FALSE(store.save("QQQ", sampleState()));
    EXPECT_FALSE(store.load("QQQ").has_value());
}
