#pragma once
// ============================================================
//  state_store.h  —  Persistance SQLite de l'état de position
//
//  Implémente trading::IStateStore : l'état du bot (inPosition,
//  buyPrice, peakPrice, holdDays, lastBarDate) survit aux
//  redémarrages. Sans cela, une position ouverte devient
//  orpheline après un restart (plus de stop-loss).
//
//  Une ligne par symbole (UPSERT). WAL comme db_logger.h.
// ============================================================
#include <sqlite3.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include "core/Interfaces.hpp"

namespace trading {

class SqliteStateStore final : public IStateStore {
public:
    explicit SqliteStateStore(const std::string& path = "swingbot_state.db") {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error("SqliteStateStore open: " + msg);
        }
        exec_("PRAGMA journal_mode=WAL;");
        exec_(R"(
            CREATE TABLE IF NOT EXISTS bot_state (
                symbol        TEXT PRIMARY KEY,
                in_position   INTEGER NOT NULL,
                buy_price     REAL    NOT NULL,
                peak_price    REAL    NOT NULL,
                hold_days     INTEGER NOT NULL,
                last_bar_date TEXT    NOT NULL,
                updated_at    TEXT    NOT NULL,
                stop_armed     INTEGER NOT NULL DEFAULT 0,
                last_exit_date TEXT    NOT NULL DEFAULT ''
            );
        )");
        // Migration d'un schéma antérieur (base créée avant B1/M2) : les
        // ALTER échouent avec « duplicate column name » sur une base déjà à
        // jour — erreur avalée exprès, tout autre échec lèverait au 1er save.
        execIgnore_("ALTER TABLE bot_state ADD COLUMN stop_armed INTEGER NOT NULL DEFAULT 0;");
        execIgnore_("ALTER TABLE bot_state ADD COLUMN last_exit_date TEXT NOT NULL DEFAULT '';");
    }

    ~SqliteStateStore() override { if (db_) sqlite3_close(db_); }

    SqliteStateStore(const SqliteStateStore&)            = delete;
    SqliteStateStore& operator=(const SqliteStateStore&) = delete;

    std::optional<BotState> load(const std::string& symbol) override {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "SELECT in_position, buy_price, peak_price, hold_days, last_bar_date,"
            " stop_armed, last_exit_date"
            " FROM bot_state WHERE symbol=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<BotState> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            BotState s;
            s.inPosition = sqlite3_column_int(stmt, 0) != 0;
            s.buyPrice   = sqlite3_column_double(stmt, 1);
            s.peakPrice  = sqlite3_column_double(stmt, 2);
            s.holdDays   = sqlite3_column_int(stmt, 3);
            const unsigned char* d = sqlite3_column_text(stmt, 4);
            s.lastBarDate = d ? reinterpret_cast<const char*>(d) : "";
            s.stopArmed   = sqlite3_column_int(stmt, 5) != 0;
            const unsigned char* x = sqlite3_column_text(stmt, 6);
            s.lastExitDate = x ? reinterpret_cast<const char*>(x) : "";
            result = s;
        }
        sqlite3_finalize(stmt);
        return result;
    }

    bool save(const std::string& symbol, const BotState& state) override {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "INSERT INTO bot_state(symbol, in_position, buy_price, peak_price,"
            " hold_days, last_bar_date, stop_armed, last_exit_date, updated_at)"
            " VALUES(?,?,?,?,?,?,?,?,datetime('now'))"
            " ON CONFLICT(symbol) DO UPDATE SET"
            "  in_position    = excluded.in_position,"
            "  buy_price      = excluded.buy_price,"
            "  peak_price     = excluded.peak_price,"
            "  hold_days      = excluded.hold_days,"
            "  last_bar_date  = excluded.last_bar_date,"
            "  stop_armed     = excluded.stop_armed,"
            "  last_exit_date = excluded.last_exit_date,"
            "  updated_at     = excluded.updated_at;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text  (stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 2, state.inPosition ? 1 : 0);
        sqlite3_bind_double(stmt, 3, state.buyPrice);
        sqlite3_bind_double(stmt, 4, state.peakPrice);
        sqlite3_bind_int   (stmt, 5, state.holdDays);
        sqlite3_bind_text  (stmt, 6, state.lastBarDate.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt, 7, state.stopArmed ? 1 : 0);
        sqlite3_bind_text  (stmt, 8, state.lastExitDate.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

private:
    sqlite3*   db_ = nullptr;
    std::mutex mtx_;

    void exec_(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "inconnue";
            sqlite3_free(err);
            throw std::runtime_error("SqliteStateStore exec: " + msg);
        }
    }

    // Variante tolérante pour les migrations : l'échec est avalé (colonne
    // déjà présente) — ne PAS l'utiliser pour autre chose que des ALTER.
    void execIgnore_(const std::string& sql) {
        char* err = nullptr;
        sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        sqlite3_free(err);
    }
};

} // namespace trading
