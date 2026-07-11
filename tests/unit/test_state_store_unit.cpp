// ============================================================
//  test_state_store_unit.cpp  —  Tests UNITAIRES
//  Cible : SqliteStateStore — persistance de l'état de position
// ============================================================
#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include "core/state_store.h"
#include "../support/TempPath.hpp"

using namespace trading;
namespace fs = std::filesystem;

// ── Fixture : un fichier SQLite temporaire par test ───────
class StateStoreUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        // Nom unique PID+compteur+tick (D59) : deux processus ctest
        // parallèles ne peuvent plus ouvrir le même fichier SQLite.
        path_ = swingbot_test::uniqueTempName("unit_state_", ".db");
    }
    void TearDown() override {
        for (const auto& suffix : {"", "-wal", "-shm"})
            if (fs::exists(path_ + suffix)) fs::remove(path_ + suffix);
    }

    static BotState sampleState() {
        BotState s;                       // champ par champ : agrégat incomplet
        s.inPosition  = true;             // = warning -Werror et ordre figé
        s.buyPrice    = 412.5;
        s.peakPrice   = 418.0;
        s.holdDays    = 3;
        s.lastBarDate = "2024-03-07";
        return s;
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
//  Champs B1/M2 — stopArmed et lastExitDate persistés
// ════════════════════════════════════════════════════════════

TEST_F(StateStoreUnit, RoundTripPersistsStopArmedAndLastExitDate) {
    SqliteStateStore store(path_);
    BotState s = sampleState();
    s.stopArmed    = true;
    s.lastExitDate = "2024-03-05";
    ASSERT_TRUE(store.save("QQQ", s));

    auto loaded = store.load("QQQ");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->stopArmed);
    EXPECT_EQ(loaded->lastExitDate, "2024-03-05");
}

// Base créée AVANT B1/M2 (schéma 7 colonnes, sans stop_armed/last_exit_date) :
// l'ouverture migre le schéma, la ligne existante se lit avec les défauts,
// et save/load fonctionnent avec les nouveaux champs.
TEST_F(StateStoreUnit, OpensAndMigratesLegacySchema) {
    {
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(path_.c_str(), &raw), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw, R"(
            CREATE TABLE bot_state (
                symbol        TEXT PRIMARY KEY,
                in_position   INTEGER NOT NULL,
                buy_price     REAL    NOT NULL,
                peak_price    REAL    NOT NULL,
                hold_days     INTEGER NOT NULL,
                last_bar_date TEXT    NOT NULL,
                updated_at    TEXT    NOT NULL
            );
            INSERT INTO bot_state VALUES
                ('QQQ', 1, 400.0, 405.0, 2, '2024-03-04', '2024-03-04');
        )", nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(raw);
    }

    SqliteStateStore store(path_);           // migration au constructeur
    auto legacy = store.load("QQQ");
    ASSERT_TRUE(legacy.has_value());
    EXPECT_TRUE(legacy->inPosition);
    EXPECT_FALSE(legacy->stopArmed);         // défaut de migration
    EXPECT_EQ(legacy->lastExitDate, "");

    BotState s = *legacy;
    s.stopArmed    = true;
    s.lastExitDate = "2024-03-06";
    ASSERT_TRUE(store.save("QQQ", s));
    auto reloaded = store.load("QQQ");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_TRUE(reloaded->stopArmed);
    EXPECT_EQ(reloaded->lastExitDate, "2024-03-06");
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
